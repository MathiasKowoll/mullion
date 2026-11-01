#include "DXBCParser/BlobContainer.h"
#include "DXBCParser/DXBCUtils.h"
#include "d3d12_device.hpp"
#include "d3d12_device_child.hpp"
#include "d3d12_pipeline.hpp"
#include "airconv_ray.h"
#include "com/com_pointer.hpp"
#include "util_md5.hpp"
#include "sha1/sha1_util.hpp"
#include "util_env.hpp"
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

// DXR state objects (DXR spec, "State objects"). a ray tracing shader is a visible function of Metal at a table slot
// the device gave it, and a shader identifier holds its shaders' slots (airconv_ray.h): a pipeline is the kernel
// that dispatches rays, linked with the functions of every export, and a function table with each at its slot

namespace mullion {

namespace {

// the name a library's function exports under: "name" of the C++ mangling "\x01?name@@..."
std::wstring
ExportName(std::string_view function) {
  if (function.starts_with("\x01?"))
    function = function.substr(2, function.find("@@") - 2);
  return std::wstring(function.begin(), function.end());
}

// MULLION_DIAG_PATH: where a refused state object's DXIL libraries and root signatures are written, as <sha1>.dxil
// and <sha1>.rootsig, with a <sha1>.txt that says what the state object was made of and why it was refused. a
// Windows path, or a macOS one through Wine's Z: drive, as the trace log's (trace::WindowsDirectory)
const std::string &
DiagDirectory() {
  static const std::string directory = [] {
    auto given = env::getEnvVar("MULLION_DIAG_PATH");
    return given.empty() || given[0] != '/' ? given : "Z:" + given;
  }();
  return directory;
}

// what a process writes there at most: enough for the libraries of a few dozen refused pipelines
constexpr uint64_t kDiagBudget = 8 << 20;

// writes a file there once: Written, or Had when an earlier call wrote it
enum DiagWritten { NotWritten, Written, Had };
DiagWritten
DiagWrite(const std::string &file, const void *data, size_t size) {
  static std::mutex mutex;
  static std::unordered_set<std::string> written;
  static uint64_t total = 0;
  static bool full = false;
  std::lock_guard lock(mutex);
  if (written.count(file))
    return Had;
  if (total + size > kDiagBudget) {
    if (!std::exchange(full, true))
      ERR("CreateStateObject: MULLION_DIAG_PATH has the ", kDiagBudget >> 20, " MB this process may write; no more");
    return NotWritten;
  }
  std::ofstream out(DiagDirectory() + "\\" + file, std::ios::binary);
  if (!out.write((const char *)data, size)) {
    ERR("CreateStateObject: could not write ", file, " to MULLION_DIAG_PATH");
    return NotWritten;
  }
  written.insert(file);
  total += size;
  return Written;
}

std::string
Narrow(std::wstring_view name) {
  std::string out;
  for (auto c : name)
    out.push_back(c < 0x80 ? (char)c : '?');
  return out;
}

const char *
KindName(uint32_t kind) {
  switch (kind) {
  case SM50_RAY_SHADER_RAY_GENERATION:
    return "ray generation shader";
  case SM50_RAY_SHADER_INTERSECTION:
    return "intersection shader";
  case SM50_RAY_SHADER_ANY_HIT:
    return "any hit shader";
  case SM50_RAY_SHADER_CLOSEST_HIT:
    return "closest hit shader";
  case SM50_RAY_SHADER_MISS:
    return "miss shader";
  case SM50_RAY_SHADER_CALLABLE:
    return "callable shader";
  default:
    return "export";
  }
}

// what a refusal is about: an export of a kind, or the state object as a whole
std::string
Subject(const std::wstring &name, uint32_t kind) {
  return name.empty() && !kind ? "the state object" : str::format(KindName(kind), " \"", Narrow(name), "\"");
}

// why a state object is refused: the export, its kind (SM50_RAY_SHADER_KIND, or 0), the step that refused it
// ("library", "compile", "validation" or "unresolved association") and what that step said
struct Refusal {
  std::wstring name;
  uint32_t kind;
  const char *step;
  std::string text;
};

struct RayLibrary {
  sm50_shader_t shader = nullptr;
  // the container itself, kept only for MULLION_DIAG_PATH: a pipeline refused for a collection's shader writes it
  std::string bytes;
  ~RayLibrary() {
    if (shader)
      SM50Destroy(shader);
  }
};

// a function of a library that is a ray tracing shader
struct RayShader {
  std::shared_ptr<RayLibrary> library;
  std::string function;
  SM50_RAY_SHADER_INFO info;
  // the collection it came from, which is a scope of its own
  const void *collection = nullptr;
};

} // namespace

// the subobjects in a library's runtime data, the container's RDAT part: a version and a count of parts, the parts'
// offsets, and each part its type, its size and its data (DXC's DxilRuntimeReflection.h). a record of the subobject
// table is DWORDs: its kind, its name, and what RDAT_SubobjectTypes.inl gives the kind
std::vector<LibrarySubobject>
ReadSubobjects(const D3D12_SHADER_BYTECODE &library) {
  std::vector<LibrarySubobject> out;
  microsoft::CDXBCParser container;
  if (FAILED(container.ReadDXBC(library.pShaderBytecode, library.BytecodeLength)))
    return out;
  auto blob = container.FindNextMatchingBlob((microsoft::DXBCFourCC)MAKEFOURCC('R', 'D', 'A', 'T'));
  if (blob == DXBC_BLOB_NOT_FOUND)
    return out;
  std::span data((const uint8_t *)container.GetBlob(blob), container.GetBlobSize(blob));
  auto word = [](std::span<const uint8_t> from, uint64_t at) {
    uint32_t value = 0;
    if (at + sizeof(value) <= from.size())
      memcpy(&value, from.data() + at, sizeof(value));
    return value;
  };
  enum { Strings = 1, Indices = 2, RawBytes = 5, Subobjects = 6, Parts };
  std::span<const uint8_t> parts[Parts];
  if (data.size() < 8 || word(data, 4) > (data.size() - 8) / sizeof(uint32_t))
    return {};
  for (uint32_t i = 0, count = word(data, 4); i < count; i++) {
    uint64_t at = word(data, 8 + (uint64_t)sizeof(uint32_t) * i);
    if (at < 8 + (uint64_t)sizeof(uint32_t) * count || at > data.size() - 8)
      return {};
    uint64_t type = word(data, at), size = word(data, at + 4);
    if (type < Parts && at + 8 + size <= data.size())
      parts[type] = data.subspan(at + 8, size);
  }
  // a string is its offset in the string part
  auto string = [&](uint32_t offset) {
    std::wstring name;
    for (auto at = offset; at < parts[Strings].size() && parts[Strings][at]; at++)
      name.push_back(parts[Strings][at]);
    return name;
  };
  uint32_t stride = word(parts[Subobjects], 4);
  if (parts[Subobjects].size() < 8 || stride < 2 * sizeof(uint32_t) ||
      word(parts[Subobjects], 0) > (parts[Subobjects].size() - 8) / stride)
    return {};
  for (uint32_t i = 0, count = word(parts[Subobjects], 0); i < count; i++) {
    auto record = parts[Subobjects].subspan(std::min<size_t>(8 + (uint64_t)i * stride, parts[Subobjects].size()));
    auto field = [&](uint32_t index) { return index * 4 < stride ? word(record, index * 4) : 0; };
    LibrarySubobject subobject{(D3D12_STATE_SUBOBJECT_TYPE)field(0), string(field(1))};
    switch (subobject.type) {
    case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE:
    case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE:
      // an offset in the raw bytes part and a size
      if ((uint64_t)field(2) + field(3) <= parts[RawBytes].size())
        subobject.bytes = parts[RawBytes].subspan(field(2), field(3));
      break;
    case D3D12_STATE_SUBOBJECT_TYPE_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
      subobject.names.push_back(string(field(2)));
      // the exports are a row of the index part: its length, then the strings
      if ((uint64_t)field(3) * sizeof(uint32_t) + sizeof(uint32_t) > parts[Indices].size() ||
          word(parts[Indices], (uint64_t)field(3) * sizeof(uint32_t)) >
              (parts[Indices].size() / sizeof(uint32_t)) - field(3) - 1)
        return {};
      for (uint32_t n = 0, row = field(3), length = word(parts[Indices], (uint64_t)row * 4); n < length; n++)
        subobject.names.push_back(string(word(parts[Indices], ((uint64_t)row + 1 + n) * 4)));
      break;
    case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG:
      subobject.values[1] = field(3);
      [[fallthrough]];
    case D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG:
      subobject.values[0] = field(2);
      break;
    case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG:
    case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1:
      subobject.values[0] = field(2);
      subobject.values[1] = subobject.type == D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1 ? field(3) : 0;
      break;
    case D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP:
      // after the group's type
      for (uint32_t n = 0; n < 3; n++)
        subobject.names.push_back(string(field(3 + n)));
      break;
    default:
      break;
    }
    out.push_back(std::move(subobject));
  }
  return out;
}

namespace {

// a hit group: its name, and the names of its closest hit, any hit and intersection shaders, empty for none
struct HitGroup {
  std::wstring name, imports[3];
};

// how an association of a subobject with exports was made, the weakest first ("Subobject association behavior"):
// what the state object says overrides what its libraries say, and of each, an association that names exports comes
// before one that names none, and that before a subobject no association of its scope gives exports
enum Strength { LibraryDeclared, LibraryDefault, LibraryExplicit, Declared, Default, Explicit };

enum SubobjectKind { Global, Local, ShaderConfig, PipelineConfig, SubobjectKinds };

struct Association {
  SubobjectKind kind;
  ID3D12RootSignature *root;
  Strength strength;
  // the export, or none for every export of the scope
  std::wstring name;
  // the scopes of one without an export: the library whose subobjects made it, and the collection it was made in
  const RayLibrary *library;
  const void *collection;
  // an unresolved library subobject (DXR "Explicit associations": the subobject need not "be visible yet")
  std::wstring subobject;
  std::pair<uint32_t, uint32_t> values{};
};

// a shader compiled with its root signatures
struct RayFunction {
  MTLD3D12Device *device;
  uint32_t slot;
  ID3D12RootSignature *global, *local;
  WMT::Reference<WMT::Function> function;

  ~RayFunction() {
    device->FreeRayFunction(slot);
  }
};

// what a shader identifier names: a ray generation, miss or callable shader, or a hit group's closest hit, any hit
// and intersection shaders, in the identifier's order
struct RayExport {
  std::shared_ptr<SM50_RAY_SHADER_IDENTIFIER> identifier = std::make_shared<SM50_RAY_SHADER_IDENTIFIER>();
  std::shared_ptr<RayFunction> functions[3];
  uint32_t kinds[3]{}; // SM50_RAY_SHADER_KIND
};

// stack sizes are counted in calls: a shader runs under the runtime function that calls it, but the ray generation
// shader, which the kernel calls. the pipeline stack size an application sums from them is then the call depth
// the pipeline needs ("Pipeline stack")
constexpr uint64_t
StackSize(uint32_t kind) {
  return kind == SM50_RAY_SHADER_RAY_GENERATION ? 1 : 2;
}

} // namespace

class MTLD3D12StateObjectImpl : public MTLD3D12DeviceChild<MTLD3D12StateObject, ID3D12StateObjectProperties> {
  D3D12_STATE_OBJECT_TYPE type_ = D3D12_STATE_OBJECT_TYPE_COLLECTION;
  uint32_t flags_ = 0;
  // every shader the state object has seen, by name, for the hit groups that import them
  std::unordered_map<std::wstring, RayShader> shaders_;
  std::unordered_map<std::wstring, RayExport> exports_;
  std::vector<Com<MTLD3D12RootSignature, false>> root_signatures_;
  D3D12_RAYTRACING_PIPELINE_CONFIG1 pipeline_config_{};
  std::optional<std::pair<uint32_t, uint32_t>> configurations_[SubobjectKinds - ShaderConfig];
  uint64_t stack_size_ = 0;
  uint64_t max_call_depth_ = UINT32_MAX;
  // the libraries of this state object's own description, whose functions its shaders may call
  std::vector<std::shared_ptr<RayLibrary>> libraries_;
  // collections retain associations and uncompiled hit groups for the objects that use their exports
  bool deferred_ = false;
  std::vector<HitGroup> deferred_hit_groups_;
  std::vector<Association> deferred_associations_;
  // the libraries' definitions by their subobjects' names, and the names of all their subobjects: an association
  // may name one of another scope
  std::unordered_map<std::wstring, Association> library_definitions_;
  std::unordered_set<std::wstring> library_names_;

  // why this state object is refused. the log has each distinct cause once a process, with the first export it was
  // found for: a pipeline of many shaders, and an application that tries the same one again, say it once
  std::vector<Refusal> refusals_;

  void
  Refuse(const char *step, const std::wstring &export_name, uint32_t kind, std::string text) {
    static std::mutex mutex;
    static std::set<std::tuple<std::string, uint32_t, std::string>> said;
    {
      std::lock_guard lock(mutex);
      if (said.insert({step, kind, text}).second)
        ERR("CreateStateObject: ", step, " refused ", Subject(export_name, kind), ": ", text);
    }
    refusals_.push_back({export_name, kind, step, std::move(text)});
  }

  struct Pipeline {
    WMT::Reference<WMT::ComputePipelineState> pipeline;
    WMT::Reference<WMT::VisibleFunctionTable> table;
  };
  // by call depth; commands in flight keep using the one they were recorded with
  mullion::mutex pipeline_lock_;
  std::map<uint32_t, Pipeline> pipelines_;

  // the shaders `pExports` takes from `all` under their new names, or all of them without a list
  template <typename Value>
  static void
  Export(
      const std::unordered_map<std::wstring, Value> &all, std::span<const D3D12_EXPORT_DESC> exports,
      std::unordered_map<std::wstring, Value> &out
  ) {
    if (exports.empty())
      out.insert(all.begin(), all.end());
    for (auto &desc : exports) {
      std::wstring name = desc.Name;
      // a mangled name exports as the function it mangles
      auto found = all.find(ExportName(std::string(name.begin(), name.end())));
      if (desc.ExportToRename) {
        std::wstring source = desc.ExportToRename;
        found = all.find(ExportName(std::string(source.begin(), source.end())));
      } else {
        name = found == all.end() ? name : found->first;
      }
      if (found != all.end())
        out.insert({name, found->second});
    }
  }

  // takes the library's shaders, and gives its subobjects: all of them, or those the exports name, under their
  // new names
  HRESULT
  AddLibrary(
      const D3D12_DXIL_LIBRARY_DESC &desc, std::vector<std::pair<const RayLibrary *, LibrarySubobject>> &subobjects
  ) {
    auto library = std::make_shared<RayLibrary>();
    if (!ShaderContainerHolds(desc.DXILLibrary.pShaderBytecode, desc.DXILLibrary.BytecodeLength)) {
      Refuse("library", {}, 0, "the DXIL library is not a container, or its hash is not the one it carries");
      return FAIL(E_INVALIDARG);
    }
    if (!ShaderFeaturesSupported(device_, desc.DXILLibrary.pShaderBytecode, desc.DXILLibrary.BytecodeLength)) {
      Refuse("library", {}, 0, "the DXIL library requires a shader feature the device does not report");
      return FAIL(E_INVALIDARG);
    }
    SM50Error error;
    if (SM50Initialize(
            desc.DXILLibrary.pShaderBytecode, desc.DXILLibrary.BytecodeLength, &library->shader, nullptr, &error
        )) {
      Refuse("library", {}, 0, std::string("not usable: ") + SM50GetErrorMessageString(error));
      return E_INVALIDARG;
    }
    if (!DiagDirectory().empty())
      library->bytes.assign((const char *)desc.DXILLibrary.pShaderBytecode, desc.DXILLibrary.BytecodeLength);
    std::unordered_map<std::wstring, RayShader> shaders;
    char name[1024];
    SM50_RAY_SHADER_INFO info;
    for (uint32_t i = 0; SM50GetRayShader(library->shader, i, name, sizeof(name), &info); i++)
      shaders.insert({ExportName(name), {library, name, info}});
    Export(shaders, {desc.pExports, desc.NumExports}, shaders_);
    libraries_.push_back(library);
    if (device_->NamesPasses())
      this->name +=
          Sha1HashState::compute(desc.DXILLibrary.pShaderBytecode, desc.DXILLibrary.BytecodeLength).string() + " ";
    for (auto &subobject : ReadSubobjects(desc.DXILLibrary)) {
      if (!desc.NumExports)
        subobjects.push_back({library.get(), subobject});
      for (auto &e : std::span(desc.pExports, desc.NumExports))
        if (subobject.name == (e.ExportToRename ? e.ExportToRename : e.Name)) {
          subobjects.push_back({library.get(), subobject});
          subobjects.back().second.name = e.Name;
        }
    }
    return S_OK;
  }

  // a library has its root signatures serialized without the container that root signature creation takes
  Com<ID3D12RootSignature>
  RootSignature(std::span<const uint8_t> serialized) {
    struct {
      microsoft::DXBCHeader header;
      uint32_t offset;
      microsoft::DXBCBlobHeader blob;
    } head{};
    std::vector<uint8_t> container(sizeof(head) + serialized.size());
    head.header = {microsoft::DXBC_FOURCC_NAME, {}, {1, 0}, (uint32_t)container.size(), 1};
    head.offset = offsetof(decltype(head), blob);
    head.blob = {microsoft::DXBC_RootSignature, (uint32_t)serialized.size()};
    memcpy(container.data(), &head, sizeof(head));
    memcpy(container.data() + sizeof(head), serialized.data(), serialized.size());
    auto hash = md5::hashDxbcBinary(container.data(), container.size());
    memcpy(container.data() + offsetof(microsoft::DXBCHeader, Hash), &hash, sizeof(hash));
    Com<ID3D12RootSignature> root;
    device_->CreateRootSignature(0, container.data(), container.size(), IID_PPV_ARGS(&root));
    return root;
  }

  // `libraries`: those whose functions the shader may call. `why` says what the converter or Metal said when it fails;
  // `unresolved` that the shader needs a function or a binding that its state object may yet get
  std::shared_ptr<RayFunction>
  Compile(
      const RayShader &shader, ID3D12RootSignature *pGlobal, ID3D12RootSignature *pLocal,
      std::span<const sm50_shader_t> libraries, bool &unresolved, std::string &why
  ) {
    auto blob = [](ID3D12RootSignature *root, SM50_SHADER_COMPILATION_ARGUMENT_TYPE type, void *next) {
      SM50_SHADER_ROOT_SIGNATURE_DATA data{next, type};
      data.bytecode_length = static_cast<MTLD3D12RootSignature *>(root)->GetBlob(&data.bytecode);
      data.static_samplers = static_cast<MTLD3D12RootSignature *>(root)->StaticSamplersAddress;
      return data;
    };
    auto function = std::make_shared<RayFunction>(device_, device_->AllocateRayFunction(), pGlobal, pLocal);
    auto name = "mullion_ray_shader_" + std::to_string(function->slot);

    SM50_SHADER_LIBRARIES_DATA linkable{nullptr, SM50_SHADER_LIBRARIES, libraries.data(), (uint32_t)libraries.size()};
    SM50_SHADER_RAY_SHADER_DATA ray{&linkable, SM50_SHADER_RAY_SHADER, shader.function.c_str()};
    void *arguments = &ray;
    SM50_SHADER_ROOT_SIGNATURE_DATA local, global;
    if (pLocal)
      arguments = &(local = blob(pLocal, SM50_SHADER_ROOT_SIGNATURE2, arguments));
    if (pGlobal)
      arguments = &(global = blob(pGlobal, SM50_SHADER_ROOT_SIGNATURE, arguments));
    SM50_SHADER_COMMON_DATA common{arguments, SM50_SHADER_COMMON, device_->GetMetalVersion()};
    common.flags = device_->GetShaderFlags();
    common.simd_width = device_->GetSIMDWidth();

    SM50ShaderBitcode bitcode;
    SM50Error error;
    if (auto result = SM50Compile(
            shader.library->shader, (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)&common, name.c_str(), &bitcode, &error
        )) {
      unresolved = result == SM50_COMPILE_UNRESOLVED;
      why = (unresolved ? "unresolved, " : "") + std::string(SM50GetErrorMessageString(error));
      return nullptr;
    }
    SM50_COMPILED_BITCODE compiled;
    SM50GetCompiledBitcode(bitcode, &compiled);
    WMT::Reference<WMT::Error> err;
    auto library = device_->GetMTLDevice().newLibrary(WMT::MakeDispatchData(compiled.Data, compiled.Size), err);
    if (library)
      function->function = library.newFunction(name.c_str());
    if (!function->function) {
      why = std::string("Metal: ") + (err ? err.description().getUTF8String() : "the library has none of its name");
      return nullptr;
    }
    return function;
  }

public:
  MTLD3D12StateObjectImpl(MTLD3D12Device *pDevice) :
      MTLD3D12DeviceChild<MTLD3D12StateObject, ID3D12StateObjectProperties>(pDevice) {}

  // `pParent` is the state object an addition grows from (AddToStateObject): it keeps its exports and their
  // identifiers
  HRESULT
  Initialize(const D3D12_STATE_OBJECT_DESC *pDesc, MTLD3D12StateObjectImpl *pParent) {
    auto start = device_->NamesPasses() ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
    type_ = pDesc->Type;
    libraries_.clear();
    if (pParent) {
      if (type_ != D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE || pParent->type_ != type_) {
        Refuse("validation", {}, 0, "an addition is a raytracing pipeline that grows a raytracing pipeline");
        return FAIL(E_INVALIDARG);
      }
      if (device_->NamesPasses())
        name = pParent->name;
      shaders_ = pParent->shaders_;
      exports_ = pParent->exports_;
      root_signatures_ = pParent->root_signatures_;
      pipeline_config_ = pParent->pipeline_config_;
      std::copy(std::begin(pParent->configurations_), std::end(pParent->configurations_), configurations_);
    }
    std::span subobjects(pDesc->pSubobjects, pDesc->NumSubobjects);

    using Subobject = const D3D12_STATE_SUBOBJECT *;
    auto kind = [](D3D12_STATE_SUBOBJECT_TYPE type) {
      return type == D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE      ? (int)Global
             : type == D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE     ? (int)Local
             : type == D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG ? (int)ShaderConfig
             : type == D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG ||
                     type == D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1
                 ? (int)PipelineConfig
                 : -1;
    };
    auto definition = [&](Subobject subobject) {
      Association made{(SubobjectKind)kind(subobject->Type)};
      if (made.kind == Global || made.kind == Local)
        made.root = ((const D3D12_GLOBAL_ROOT_SIGNATURE *)subobject->pDesc)->pGlobalRootSignature;
      else if (made.kind == ShaderConfig) {
        auto &config = *(const D3D12_RAYTRACING_SHADER_CONFIG *)subobject->pDesc;
        made.values = {config.MaxPayloadSizeInBytes, config.MaxAttributeSizeInBytes};
      } else if (made.kind == PipelineConfig) {
        made.values.first = ((const D3D12_RAYTRACING_PIPELINE_CONFIG *)subobject->pDesc)->MaxTraceRecursionDepth;
        if (subobject->Type == D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1)
          made.values.second = ((const D3D12_RAYTRACING_PIPELINE_CONFIG1 *)subobject->pDesc)->Flags;
      }
      return made;
    };
    auto same = [](const Association &a, const Association &b) {
      if (a.kind >= ShaderConfig)
        return a.values == b.values;
      if (a.root == b.root)
        return true;
      if (!a.root || !b.root)
        return false;
      const void *first, *second;
      static_cast<MTLD3D12RootSignature *>(a.root)->GetBlob(&first);
      static_cast<MTLD3D12RootSignature *>(b.root)->GetBlob(&second);
      UINT size, other_size;
      return SUCCEEDED(microsoft::DXBCGetRootSignature(first, &first, &size)) &&
             SUCCEEDED(microsoft::DXBCGetRootSignature(second, &second, &other_size)) && size == other_size &&
             !memcmp(first, second, size);
    };
    bool failed = false;
    auto config_name = [](unsigned of) {
      const char *names[] = {"global root signature", "local root signature", "shader config", "pipeline config"};
      return of < std::size(names) ? names[of] : "subobject";
    };
    // `name` and `kind`: the export whose config it is, or none for a collection's
    auto configuration = [&](SubobjectKind of, const std::pair<uint32_t, uint32_t> &values, const std::wstring &name,
                             uint32_t kind) {
      auto &seen = configurations_[of - ShaderConfig];
      if (seen && *seen != values) {
        Refuse("validation", name, kind,
               str::format("its ", config_name(of), " (", values.first, ", ", values.second, ") is not the (",
                           seen->first, ", ", seen->second, ") of the state object's other exports"));
        failed = true;
      }
      seen = values;
    };
    std::vector<Association> associations;
    std::unordered_set<const RayLibrary *> configured;
    if (pParent)
      for (auto &[name, shader] : pParent->shaders_)
        configured.insert(shader.library.get());
    // the state object's subobjects that an association of its own gives exports
    std::unordered_set<Subobject> associated;
    std::vector<HitGroup> hit_groups;
    std::vector<std::pair<const RayLibrary *, LibrarySubobject>> library_subobjects;
    for (auto &subobject : subobjects) {
      switch (subobject.Type) {
      case D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG:
        flags_ |= ((const D3D12_STATE_OBJECT_CONFIG *)subobject.pDesc)->Flags;
        break;
      case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE:
      case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE:
        root_signatures_.push_back(static_cast<MTLD3D12RootSignature *>(definition(&subobject).root));
        break;
      case D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY:
        if (HRESULT hr = AddLibrary(*(const D3D12_DXIL_LIBRARY_DESC *)subobject.pDesc, library_subobjects); FAILED(hr))
          return hr;
        break;
      case D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION: {
        auto &desc = *(const D3D12_EXISTING_COLLECTION_DESC *)subobject.pDesc;
        auto collection = static_cast<MTLD3D12StateObjectImpl *>(desc.pExistingCollection);
        if (device_->NamesPasses())
          name += collection->name;
        Export(collection->shaders_, {desc.pExports, desc.NumExports}, shaders_);
        Export(collection->exports_, {desc.pExports, desc.NumExports}, exports_);
        if (!collection->deferred_) {
          for (auto &[name, shader] : collection->shaders_)
            configured.insert(shader.library.get());
          for (unsigned of = ShaderConfig; of < SubobjectKinds; of++)
            if (auto &config = collection->configurations_[of - ShaderConfig]; config)
              configuration((SubobjectKind)of, *config, L"(a collection)", 0);
        }
        root_signatures_.insert(
            root_signatures_.end(), collection->root_signatures_.begin(), collection->root_signatures_.end()
        );
        // what it left for this state object: its hit groups, as the exports take them, and its associations
        std::unordered_map<std::wstring, HitGroup> groups, taken;
        for (auto &group : collection->deferred_hit_groups_)
          groups[group.name] = group;
        Export(groups, {desc.pExports, desc.NumExports}, taken);
        for (auto &[name, group] : taken)
          hit_groups.push_back({name, {group.imports[0], group.imports[1], group.imports[2]}});
        // an association follows its export to the name it is taken under
        for (auto &association : collection->deferred_associations_) {
          if (association.name.empty() || !desc.NumExports)
            associations.push_back(association);
          for (auto &taken_as : std::span(desc.pExports, desc.NumExports))
            if (!association.name.empty() &&
                association.name == (taken_as.ExportToRename ? taken_as.ExportToRename : taken_as.Name)) {
              associations.push_back(association);
              associations.back().name = taken_as.Name;
            }
        }
        library_definitions_.insert(collection->library_definitions_.begin(), collection->library_definitions_.end());
        library_names_.insert(collection->library_names_.begin(), collection->library_names_.end());
        libraries_.insert(libraries_.end(), collection->libraries_.begin(), collection->libraries_.end());
        break;
      }
      case D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP: {
        auto &group = *(const D3D12_HIT_GROUP_DESC *)subobject.pDesc;
        auto name = [](const WCHAR *import) { return std::wstring(import ? import : L""); };
        hit_groups.push_back(
            {group.HitGroupExport,
             {name(group.ClosestHitShaderImport), name(group.AnyHitShaderImport), name(group.IntersectionShaderImport)}}
        );
        break;
      }
      case D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION: {
        auto &association = *(const D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION *)subobject.pDesc;
        auto of = kind(association.pSubobjectToAssociate->Type);
        if (of < 0)
          break;
        auto made = definition(association.pSubobjectToAssociate);
        // without exports it declares the default ("Declaring a default association")
        made.strength = Default;
        if (!association.NumExports)
          associations.push_back(made);
        else
          associated.insert(association.pSubobjectToAssociate);
        for (auto name : std::span(association.pExports, association.NumExports)) {
          made.strength = Explicit;
          made.name = name;
          associations.push_back(made);
        }
        break;
      }
      default:
        break;
      }
    }
    // as does a subobject that no association gives exports
    for (auto &subobject : subobjects)
      if (auto of = kind(subobject.Type); of >= 0 && !associated.count(&subobject)) {
        auto made = definition(&subobject);
        made.strength = Declared;
        associations.push_back(made);
      }

    // the libraries' subobjects ("Subobjects in DXIL libraries"): a library is a scope of its own
    auto &library_definitions = library_definitions_;
    for (auto &[library, subobject] : library_subobjects) {
      library_names_.insert(subobject.name);
      switch (subobject.type) {
      case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE:
      case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE: {
        auto root = RootSignature(subobject.bytes);
        if (!root) {
          Refuse("library", subobject.name, 0, "the library's root signature of this name is not usable");
          return E_INVALIDARG;
        }
        root_signatures_.push_back(static_cast<MTLD3D12RootSignature *>(root.ptr()));
        library_definitions[subobject.name] = {
            (SubobjectKind)kind(subobject.type), root.ptr(), LibraryDeclared, {}, library};
        break;
      }
      case D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG:
        flags_ |= subobject.values[0];
        break;
      case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG:
      case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG:
      case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1:
        library_definitions[subobject.name] = {
            (SubobjectKind)kind(subobject.type),       nullptr, LibraryDeclared, {}, library, nullptr, {},
            {subobject.values[0], subobject.values[1]}};
        break;
      case D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP:
        // a library names any hit before closest hit
        hit_groups.push_back({subobject.name, {subobject.names[1], subobject.names[0], subobject.names[2]}});
        break;
      default:
        break;
      }
    }
    // an association of a library's subobject: one of a library's own (`library`), or the state object's. a
    // subobject no library defines here waits under its name
    auto associate = [&](const RayLibrary *library, const std::wstring &name, std::span<const std::wstring> exports) {
      auto found = library_definitions.find(name);
      if (found == library_definitions.end() && library_names_.count(name))
        return;
      auto made = found == library_definitions.end() ? Association{SubobjectKinds} : found->second;
      made.library = library;
      if (found == library_definitions.end())
        made.subobject = name;
      made.strength = library ? LibraryDefault : Default;
      if (exports.empty())
        associations.push_back(made);
      for (auto &e : exports) {
        made.strength = library ? LibraryExplicit : Explicit;
        made.name = e;
        associations.push_back(made);
      }
    };
    std::unordered_set<std::wstring> library_associated;
    for (auto &[library, subobject] : library_subobjects)
      if (subobject.type == D3D12_STATE_SUBOBJECT_TYPE_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION) {
        associate(library, subobject.names[0], std::span(subobject.names).subspan(1));
        if (subobject.names.size() > 1)
          library_associated.insert(subobject.names[0]);
      }
    for (auto &[library, subobject] : library_subobjects)
      if (auto of = kind(subobject.type); of >= 0 && !library_associated.count(subobject.name))
        associations.push_back(library_definitions[subobject.name]);
    for (auto &subobject : subobjects)
      if (subobject.Type == D3D12_STATE_SUBOBJECT_TYPE_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION) {
        auto &association = *(const D3D12_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION *)subobject.pDesc;
        std::vector<std::wstring> exports(association.pExports, association.pExports + association.NumExports);
        associate(nullptr, association.SubobjectToAssociate, exports);
      }
    // DXR "Explicit associations": an executable state object needs both ends of an association resolved
    for (auto &association : associations) {
      if (association.subobject.empty())
        continue;
      if (auto found = library_definitions.find(association.subobject); found != library_definitions.end()) {
        association.kind = found->second.kind;
        association.root = found->second.root;
        association.values = found->second.values;
        association.subobject.clear();
      } else if (library_names_.count(association.subobject)) {
        association.subobject.clear();
      } else if (type_ == D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE) {
        Refuse("unresolved association", association.name, 0,
               "an association names the subobject \"" + Narrow(association.subobject) +
                   "\", which no library defines");
        return E_INVALIDARG;
      }
    }

    if (pParent && (!(flags_ & D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS) ||
                    !(pParent->flags_ & D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS))) {
      Refuse("validation", {}, 0, "an addition and the state object it grows must both allow additions");
      return FAIL(E_INVALIDARG);
    }
    deferred_ = type_ == D3D12_STATE_OBJECT_TYPE_COLLECTION &&
                (flags_ & D3D12_STATE_OBJECT_FLAG_ALLOW_LOCAL_DEPENDENCIES_ON_EXTERNAL_DEFINITIONS);
    if (type_ == D3D12_STATE_OBJECT_TYPE_COLLECTION) {
      for (auto &[name, shader] : shaders_)
        shader.collection = this;
      for (auto &association : associations)
        association.collection = association.collection ? association.collection : this;
    }

    std::vector<sm50_shader_t> linkable;
    for (auto &library : libraries_)
      linkable.push_back(library->shader);
    bool unresolved = false;
    // the subobject of a shader: the strongest association with the shader, by the name it exports under or,
    // from its own library, the name it has there, with its hit group ("Subobject associations for hit groups"), or
    // with every export of a scope the shader is in. two of one strength that name exports must agree
    // ("Conflicting subobject associations")
    // a shader that a collection left for this state object keeps what the collection associated with it: this
    // state object's associations "do not override any existing associations in contained collections". one of its
    // own that names the shader is a conflict with one of the collection's that does, and stands where the
    // collection gave the shader only a default
    // the subobject an association names that no library here defines, for the log
    std::wstring missing_subobject;
    auto resolve = [&](SubobjectKind of, const std::wstring &name, const RayShader &shader, const std::wstring *group) {
      auto source = ExportName(shader.function);
      const Association *best[2] = {};
      auto applies = [&](const Association &a) {
        return a.name.empty() ? (!a.library || a.library == shader.library.get()) &&
                                    (!a.collection || a.collection == shader.collection)
                              : a.name == name || (group && a.name == *group) ||
                                    (a.library == shader.library.get() && a.name == source);
      };
      bool ambiguous[std::size(best)] = {};
      for (auto &a : associations) {
        unsigned scope = shader.collection && a.collection == shader.collection;
        auto &strongest = best[scope];
        if (!applies(a))
          continue;
        if (!a.subobject.empty()) {
          unresolved = true;
          missing_subobject = a.subobject;
        }
        if (a.kind != of)
          continue;
        if (!strongest || a.strength > strongest->strength) {
          strongest = &a;
          ambiguous[scope] = false;
        } else if (a.strength == strongest->strength)
          ambiguous[scope] |= !same(a, *strongest);
      }
      for (unsigned i = 0; i < std::size(best); i++)
        if (ambiguous[i]) {
          if (!best[i]->name.empty()) {
            Refuse("validation", name, shader.info.kind,
                   str::format("two associations that name it give it different ", config_name(of), "s"));
            failed = true;
          }
          best[i] = nullptr;
        }
      auto [outer, inner] = best;
      if (!outer || !inner) {
        if (!outer && !inner && of >= ShaderConfig && (ambiguous[0] || ambiguous[1])) {
          Refuse("validation", name, shader.info.kind,
                 str::format("two default associations give it different ", config_name(of), "s"));
          failed = true;
        }
        return outer ? outer : inner;
      }
      if (!outer->name.empty() && !inner->name.empty() && !same(*outer, *inner)) {
        Refuse("validation", name, shader.info.kind,
               str::format("an association gives it a ", config_name(of), " other than its collection's"));
        failed = true;
      }
      return outer->name.empty() ? inner : outer;
    };
    auto validate = [&](const std::wstring &name, const RayShader &shader, const std::wstring *group, bool inherited) {
      // DXR "Subobject association requirements": shader and pipeline configs are required and matching for all exports
      for (unsigned of = ShaderConfig; of < SubobjectKinds; of++) {
        auto config = resolve((SubobjectKind)of, name, shader, group);
        if (!config) {
          if (inherited && configurations_[of - ShaderConfig])
            continue;
          unresolved = true;
          if (!deferred_ && !failed) {
            Refuse("validation", name, shader.info.kind, str::format("no ", config_name(of), " is associated with it"));
            failed = true;
          }
          continue;
        }
        configuration((SubobjectKind)of, config->values, name, shader.info.kind);
      }
    };
    // a shader compiles once for each pair of root signatures it is used with
    std::map<std::tuple<sm50_shader_t, std::string, ID3D12RootSignature *, ID3D12RootSignature *>,
             std::shared_ptr<RayFunction>>
        compiled;
    auto function = [&](RayExport &to, unsigned index, const std::wstring &name, const std::wstring *group) {
      auto shader = shaders_.find(name);
      if (shader == shaders_.end()) {
        if (!deferred_) {
          Refuse("validation", group ? *group : name, 0,
                 "the hit group imports \"" + Narrow(name) + "\", which no library exports");
          failed = true;
        }
        return false;
      }
      unresolved = false;
      validate(name, shader->second, group, configured.count(shader->second.library.get()));
      if (failed)
        return false;
      auto global_association = resolve(Global, name, shader->second, group),
           local_association = resolve(Local, name, shader->second, group);
      auto global = global_association ? global_association->root : nullptr,
           local = local_association ? local_association->root : nullptr;
      if (failed || unresolved) {
        if (!failed && !deferred_)
          Refuse("unresolved association", name, shader->second.info.kind,
                 "an association names the subobject \"" + Narrow(missing_subobject) + "\", which no library defines");
        failed |= !deferred_;
        return false;
      }
      auto previous = to.functions[index];
      if (previous && same({Global, global}, {Global, previous->global}) &&
          same({Local, local}, {Local, previous->local}))
        return true;
      auto &made = compiled[{shader->second.library->shader, shader->second.function, global, local}];
      std::string why;
      if (!made && !(made = Compile(shader->second, global, local, linkable, unresolved, why))) {
        if (!deferred_ || !unresolved) {
          Refuse("compile", name, shader->second.info.kind, why);
          failed = true;
        }
        return false;
      }
      if (previous)
        to.identifier = std::make_shared<SM50_RAY_SHADER_IDENTIFIER>(*to.identifier);
      to.functions[index] = made;
      to.kinds[index] = shader->second.info.kind;
      to.identifier->function[index] = made->slot;
      return true;
    };
    for (auto &[name, shader] : shaders_) {
      auto of = shader.info.kind;
      bool named = of == SM50_RAY_SHADER_RAY_GENERATION || of == SM50_RAY_SHADER_MISS || of == SM50_RAY_SHADER_CALLABLE;
      if (named && (!pParent || !pParent->shaders_.count(name))) {
        RayExport to = exports_.count(name) ? exports_.at(name) : RayExport{};
        if (function(to, 0, name, nullptr))
          exports_.insert_or_assign(name, std::move(to));
      } else if ((!pParent || !pParent->shaders_.count(name)) &&
               std::none_of(hit_groups.begin(), hit_groups.end(), [&](const HitGroup &group) {
                 return std::find(std::begin(group.imports), std::end(group.imports), name) != std::end(group.imports);
               })) {
        if (configured.count(shader.library.get())) {
          unresolved = false;
          validate(name, shader, nullptr, configured.count(shader.library.get()));
          if (unresolved && !deferred_ && !failed)
            Refuse("unresolved association", name, shader.info.kind,
                   "an association names the subobject \"" + Narrow(missing_subobject) +
                       "\", which no library defines");
          failed |= unresolved && !deferred_;
        } else {
          RayExport checked;
          function(checked, 0, name, nullptr);
        }
      }
    }
    for (auto &group : hit_groups) {
      if (exports_.count(group.name)) {
        Refuse("validation", group.name, 0, "another export has the hit group's name");
        return E_INVALIDARG;
      }
      RayExport to;
      bool complete = true;
      for (unsigned i = 0; i < std::size(group.imports); i++)
        if (!group.imports[i].empty())
          complete &= function(to, i, group.imports[i], &group.name);
      if (complete)
        exports_.insert({group.name, std::move(to)});
      else if (deferred_)
        deferred_hit_groups_.push_back(group);
    }
    if (type_ == D3D12_STATE_OBJECT_TYPE_COLLECTION)
      deferred_associations_ = std::move(associations);
    if (failed) {
      if (refusals_.empty())
        Refuse("validation", {}, 0, "refused without a cause it could name");
      return FAIL(E_INVALIDARG);
    }
    if (auto &config = configurations_[PipelineConfig - ShaderConfig]; config)
      pipeline_config_ = {config->first, (D3D12_RAYTRACING_PIPELINE_FLAGS)config->second};

    // "Default pipeline stack size": the deepest any chain of the shaders there are can get, with callable shaders
    // two deep
    if (type_ == D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE) {
      std::unordered_map<uint32_t, uint64_t> most;
      for (auto &[_, e] : exports_)
        for (auto of : e.kinds)
          if (of)
            most[of] = StackSize(of);
      uint64_t recursion = pipeline_config_.MaxTraceRecursionDepth;
      uint64_t hit = std::max(most[SM50_RAY_SHADER_CLOSEST_HIT], most[SM50_RAY_SHADER_MISS]);
      stack_size_ = most[SM50_RAY_SHADER_RAY_GENERATION] +
                    std::max(hit, most[SM50_RAY_SHADER_INTERSECTION] + most[SM50_RAY_SHADER_ANY_HIT]) *
                        std::min<uint64_t>(1, recursion) +
                    hit * (std::max<uint64_t>(recursion, 1) - 1) + 2 * most[SM50_RAY_SHADER_CALLABLE];
      // without callable recursion, DXR's ray recursion limit bounds every native call chain, regardless of spare
      // bytes requested by SetPipelineStackSize (DXR spec, "Ray recursion limit"; Metal maxCallStackDepth)
      if (!most[SM50_RAY_SHADER_CALLABLE])
        max_call_depth_ = stack_size_;
      if (pParent)
        stack_size_ = pParent->stack_size_;
    }
    if (device_->NamesPasses() && type_ == D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE)
      device_->PipelineMade(name, "ray", start);
    return S_OK;
  }

  // MULLION_DIAG_PATH: what a refused state object was made of, to make it again outside the application. its
  // libraries, those of its collections and its root signatures are written by their SHA-1, and a <sha1>.txt says
  // which subobjects named them and why the state object was refused
  void
  WriteRefusal(const D3D12_STATE_OBJECT_DESC &desc) {
    if (DiagDirectory().empty())
      return;
    auto library = [&](const void *data, size_t size) {
      auto hash = Sha1HashState::compute(data, size).string();
      return hash + (DiagWrite(hash + ".dxil", data, size) ? ".dxil" : ".dxil (not written)");
    };
    auto root = [&](ID3D12RootSignature *root) -> std::string {
      if (!root)
        return "none";
      const void *data;
      size_t size = static_cast<MTLD3D12RootSignature *>(root)->GetBlob(&data);
      auto hash = Sha1HashState::compute(data, size).string();
      return hash + (DiagWrite(hash + ".rootsig", data, size) ? ".rootsig" : ".rootsig (not written)");
    };
    auto names = [](const WCHAR *const *names, UINT count) {
      std::string out;
      for (auto name : std::span(names, count))
        out += str::format(" \"", Narrow(name ? name : L""), "\"");
      return count ? out : std::string(" (every export: a default)");
    };
    auto exports = [](const D3D12_EXPORT_DESC *exports, UINT count) {
      std::string out;
      for (auto &e : std::span(exports, count))
        out += str::format(
            " \"", Narrow(e.Name ? e.Name : L""), "\"",
            e.ExportToRename ? " from \"" + Narrow(e.ExportToRename) + "\"" : std::string()
        );
      return count ? out : std::string(" (all)");
    };
    auto subobjects = std::span(desc.pSubobjects, desc.NumSubobjects);
    std::string text = str::format(
        desc.Type == D3D12_STATE_OBJECT_TYPE_COLLECTION ? "collection" : "raytracing pipeline", ", ",
        subobjects.size(), " subobjects\n"
    );
    for (auto &refusal : refusals_)
      text += str::format(
          "refused: ", refusal.step, ": ", Subject(refusal.name, refusal.kind), ": ", refusal.text, "\n"
      );
    for (size_t i = 0; i < subobjects.size(); i++) {
      auto &subobject = subobjects[i];
      text += str::format(i, ": ");
      switch (subobject.Type) {
      case D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG:
        text += str::format("state object config, flags ", (uint32_t)((const D3D12_STATE_OBJECT_CONFIG *)subobject.pDesc)->Flags);
        break;
      case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE:
      case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE:
        text += str::format(
            subobject.Type == D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE ? "global" : "local", " root signature ",
            root(((const D3D12_GLOBAL_ROOT_SIGNATURE *)subobject.pDesc)->pGlobalRootSignature)
        );
        break;
      case D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY: {
        auto &code = *(const D3D12_DXIL_LIBRARY_DESC *)subobject.pDesc;
        text += "library " + library(code.DXILLibrary.pShaderBytecode, code.DXILLibrary.BytecodeLength) + ", exports" +
                exports(code.pExports, code.NumExports);
        break;
      }
      case D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION: {
        auto &existing = *(const D3D12_EXISTING_COLLECTION_DESC *)subobject.pDesc;
        auto collection = static_cast<MTLD3D12StateObjectImpl *>(existing.pExistingCollection);
        text += "collection, exports" + exports(existing.pExports, existing.NumExports) + ", libraries";
        for (auto &taken : collection->libraries_)
          text += " " + (taken->bytes.empty() ? std::string("(not kept)") : library(taken->bytes.data(), taken->bytes.size()));
        text += ", root signatures";
        for (auto &taken : collection->root_signatures_)
          text += " " + root(taken.ptr());
        break;
      }
      case D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION: {
        auto &association = *(const D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION *)subobject.pDesc;
        auto at = association.pSubobjectToAssociate - desc.pSubobjects;
        text += str::format("association of subobject ", at, " with") + names(association.pExports, association.NumExports);
        break;
      }
      case D3D12_STATE_SUBOBJECT_TYPE_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION: {
        auto &association = *(const D3D12_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION *)subobject.pDesc;
        text += "association of library subobject \"" + Narrow(association.SubobjectToAssociate) + "\" with" +
                names(association.pExports, association.NumExports);
        break;
      }
      case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG: {
        auto &config = *(const D3D12_RAYTRACING_SHADER_CONFIG *)subobject.pDesc;
        text += str::format("shader config, payload ", config.MaxPayloadSizeInBytes, ", attributes ",
                            config.MaxAttributeSizeInBytes);
        break;
      }
      case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG:
      case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1: {
        auto &config = *(const D3D12_RAYTRACING_PIPELINE_CONFIG1 *)subobject.pDesc;
        text += str::format(
            "pipeline config, recursion ", config.MaxTraceRecursionDepth, ", flags ",
            subobject.Type == D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1 ? (uint32_t)config.Flags : 0u
        );
        break;
      }
      case D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP: {
        auto &group = *(const D3D12_HIT_GROUP_DESC *)subobject.pDesc;
        auto name = [](const WCHAR *import) { return import ? "\"" + Narrow(import) + "\"" : std::string("none"); };
        text += str::format(
            "hit group \"", Narrow(group.HitGroupExport ? group.HitGroupExport : L""), "\", type ", (uint32_t)group.Type,
            ", closest hit ", name(group.ClosestHitShaderImport), ", any hit ", name(group.AnyHitShaderImport),
            ", intersection ", name(group.IntersectionShaderImport)
        );
        break;
      }
      default:
        text += str::format("subobject of type ", (uint32_t)subobject.Type);
        break;
      }
      text += "\n";
    }
    auto hash = Sha1HashState::compute(text.data(), text.size()).string();
    if (DiagWrite(hash + ".txt", text.data(), text.size()) == Written)
      ERR("CreateStateObject: wrote what it refused to MULLION_DIAG_PATH as ", hash, ".txt");
  }

  HRESULT STDMETHODCALLTYPE
  QueryInterface(REFIID riid, void **ppvObject) {
    if (ppvObject == nullptr)
      return FAIL(E_POINTER);
    *ppvObject = nullptr;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
        riid == __uuidof(ID3D12Pageable) || riid == __uuidof(ID3D12StateObject)) {
      *ppvObject = ref(static_cast<MTLD3D12StateObject *>(this));
      return S_OK;
    }
    if (riid == __uuidof(ID3D12StateObjectProperties)) {
      *ppvObject = ref(static_cast<ID3D12StateObjectProperties *>(this));
      return S_OK;
    }
    if (logQueryInterfaceError(__uuidof(ID3D12StateObject), riid))
      WARN("D3D12StateObject: Unknown interface query ", str::format(riid));
    return MTLD3D12DeviceChild::QueryInterface(riid, ppvObject);
  }

  void *STDMETHODCALLTYPE
  GetShaderIdentifier(const WCHAR *pExportName) {
    auto found = exports_.find(pExportName);
    return found == exports_.end() ? nullptr : found->second.identifier.get();
  }

  // a hit group's shaders are named "group::closesthit", "group::anyhit" and "group::intersection"; the group
  // itself has no size, nor has another shader a part
  UINT64 STDMETHODCALLTYPE
  GetShaderStackSize(const WCHAR *pExportName) {
    std::wstring name = pExportName;
    unsigned index = 0;
    auto at = name.find(L"::");
    bool part = at != std::wstring::npos;
    if (part) {
      const std::wstring_view parts[] = {L"closesthit", L"anyhit", L"intersection"};
      index = std::find(std::begin(parts), std::end(parts), std::wstring_view(name).substr(at + 2)) - parts;
      name.resize(at);
    }
    auto found = exports_.find(name);
    if (found == exports_.end() || index >= std::size(found->second.kinds))
      return 0xffffffff;
    auto kind = found->second.kinds[index];
    bool of_group = kind == SM50_RAY_SHADER_CLOSEST_HIT || kind == SM50_RAY_SHADER_ANY_HIT ||
                    kind == SM50_RAY_SHADER_INTERSECTION;
    return !kind || part != of_group ? 0xffffffff : StackSize(kind);
  }

  UINT64 STDMETHODCALLTYPE
  GetPipelineStackSize() {
    return stack_size_;
  }

  void STDMETHODCALLTYPE
  SetPipelineStackSize(UINT64 PipelineStackSizeInBytes) {
    if (PipelineStackSizeInBytes < 0xffffffff)
      stack_size_ = PipelineStackSizeInBytes;
  }

  bool
  GetPipeline(WMT::ComputePipelineState &Pipeline, WMT::VisibleFunctionTable &Table, uint32_t &Flags) {
    std::unique_lock<mullion::mutex> lock(pipeline_lock_);
    Flags = pipeline_config_.Flags;
    uint32_t depth = std::max<uint64_t>(std::min(stack_size_, max_call_depth_), 1);
    auto &made = pipelines_[depth];
    if (!made.pipeline) {
      auto start = device_->NamesPasses() ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
      auto library = device_->GetLib().getLibrary();
      WMT::Reference<WMT::Function> runtime[] = {
          library.newFunction("mullion_ray_trace"), library.newFunction("mullion_ray_report_hit"),
          library.newFunction("mullion_ray_call")
      };
      // the table has each function at its slot; the pipeline links them all
      std::vector<obj_handle_t> slots(SM50_RAY_FUNCTION_FIRST_SHADER), linked;
      slots[SM50_RAY_FUNCTION_TRACE] = runtime[0];
      slots[SM50_RAY_FUNCTION_REPORT_HIT] = runtime[1];
      slots[SM50_RAY_FUNCTION_CALL] = runtime[2];
      for (auto &[_, e] : exports_)
        for (auto &function : e.functions)
          if (function) {
            slots.resize(std::max<size_t>(slots.size(), function->slot + 1));
            slots[function->slot] = function->function;
          }
      for (auto function : slots)
        if (function)
          linked.push_back(function);
      auto kernel = library.newFunction("dispatch_rays");
      WMTLinkedComputePipelineInfo info{};
      info.compute_function = kernel;
      info.functions.set(linked.data());
      info.function_count = linked.size();
      info.max_call_stack_depth = depth;
      WMT::Reference<WMT::Error> err;
      made.pipeline = device_->GetMTLDevice().newLinkedComputePipelineState(info, err);
      if (!made.pipeline) {
        ERR("Failed to create a ray tracing pipeline: ", err ? err.description().getUTF8String() : "");
        return false;
      }
      made.table = made.pipeline.newVisibleFunctionTable(slots.data(), slots.size());
      if (device_->NamesPasses())
        device_->PipelineMade(name, "ray", start);
    }
    Pipeline = made.pipeline;
    Table = made.table;
    return true;
  }
};

HRESULT
CreateStateObject(
    MTLD3D12Device *pDevice, const D3D12_STATE_OBJECT_DESC *pDesc, ID3D12StateObject *pParent, REFIID riid,
    void **ppStateObject
) {
  InitReturnPtr(ppStateObject);
  if (!pDesc)
    return FAIL(E_INVALIDARG);
  auto state = Com(new MTLD3D12StateObjectImpl(pDevice));
  HRESULT hr = state->Initialize(pDesc, static_cast<MTLD3D12StateObjectImpl *>(pParent));
  if (FAILED(hr)) {
    state->WriteRefusal(*pDesc);
    return hr;
  }
  return state->QueryInterface(riid, ppStateObject);
}

} // namespace mullion
