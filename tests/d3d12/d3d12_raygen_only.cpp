// contract: a raytracing pipeline whose only shader is a ray generation shader is a whole pipeline (DXR functional
// spec, https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html, "Subobject association requirements": a shader
// config and a pipeline config are "Required & matching for all exports", root signatures "Optional"). nothing asks
// for a hit group or a miss shader: "a NULL shader identifier ... indicates no shader" ("Shader identifier"), so a ray
// a generation shader traces with a null miss record misses and leaves its payload as it was. the shapes are those
// Unreal Engine 5 gives its ray generation passes: a library export renamed (D3D12_EXPORT_DESC), the configs and a
// local root signature associated with that name (SUBOBJECT_TO_EXPORTS_ASSOCIATION), or with no export list at all,
// beside a miss and a hit shader and a function the pipeline never uses; a payload of no bytes, a recursion depth of
// zero or one ("MaxTraceRecursionDepth ... Limit on ray recursion"), PIPELINE_CONFIG1 flags, inline ray queries
// (SM 6.5) and descriptor heap indexing (SM 6.6) in the generation shader; one made from a collection
// ("Collection state object") that compiled it whole, with or without its own configs. each is created and its
// identifier dispatched over 64 threads, which write what the contract says to a readback buffer.
#include "d3d12_test.hpp"
#include <functional>

static const char hlsl[] = R"hlsl(
RaytracingAccelerationStructure scene : register(t0);
RWStructuredBuffer<uint> results : register(u0);
struct Payload { uint value; };
[shader("raygeneration")] void traced() {
  Payload p = { DispatchRaysIndex().x + 100 };
  RayDesc ray = { float3(0, 0, 0), 0, float3(0, 0, 1), 1 };
  TraceRay(scene, RAY_FLAG_NONE, 0xff, 0, 0, 0, ray, p);
  results[DispatchRaysIndex().x] = p.value;
}
[shader("raygeneration")] void plain() { results[DispatchRaysIndex().x] = DispatchRaysIndex().x + 100; }
[shader("miss")] void unused_miss(inout Payload p) { p.value = 1; }
[shader("closesthit")]
void unused_hit(inout Payload p, in BuiltInTriangleIntersectionAttributes a) { p.value = 2; }
export uint unused_function(uint value) { return value + 3; }
)hlsl";

// an inline query against no acceleration structure commits nothing
static const char query_hlsl[] = R"hlsl(
RaytracingAccelerationStructure scene : register(t0);
RWStructuredBuffer<uint> results : register(u0);
[shader("raygeneration")] void queried() {
  RayQuery<RAY_FLAG_NONE> q;
  RayDesc ray = { float3(0, 0, 0), 0, float3(0, 0, 1), 1 };
  q.TraceRayInline(scene, RAY_FLAG_NONE, 0xff, ray);
  while (q.Proceed()) {}
  results[DispatchRaysIndex().x] = DispatchRaysIndex().x + 100 + q.CommittedStatus();
}
)hlsl";

static const char heap_hlsl[] = R"hlsl(
[shader("raygeneration")] void indexed() {
  RWStructuredBuffer<uint> results = ResourceDescriptorHeap[0];
  results[DispatchRaysIndex().x] = DispatchRaysIndex().x + 100;
}
)hlsl";

int
main(int argc, char **argv) {
  Compiler compiler;
  if (!front_end(argc, argv, compiler) || !compiler.dxc) {
    printf("skipped: ray tracing libraries need DXC\n");
    return 77;
  }
  step("compile the libraries");
  auto library = compiler.compile(hlsl, "", "lib_6_3");
  auto query = compiler.compile(query_hlsl, "", "lib_6_5");
  auto heap = compiler.compile(heap_hlsl, "", "lib_6_6");
  if (library.empty() || query.empty() || heap.empty()) {
    printf("failed: HLSL did not compile\n");
    return 1;
  }
  ComPtr<ID3D12Device5> device;
  CHECK(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
  D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
  device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options));
  if (options.RaytracingTier < D3D12_RAYTRACING_TIER_1_1) {
    printf("skipped: no ray tracing tier 1.1\n");
    return 77;
  }
  D3D12_ROOT_PARAMETER parameters[2]{{D3D12_ROOT_PARAMETER_TYPE_UAV}, {D3D12_ROOT_PARAMETER_TYPE_SRV}};
  auto global = root_signature(device.Get(), {2, parameters});
  D3D12_ROOT_SIGNATURE_DESC local_desc{0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE};
  auto local = root_signature(device.Get(), local_desc);
  D3D12_ROOT_PARAMETER constants{D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS};
  constants.Constants = {0, 1, 2};
  D3D12_ROOT_SIGNATURE_DESC constants_desc{1, &constants, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE};
  auto local_constants = root_signature(device.Get(), constants_desc);
  D3D12_ROOT_SIGNATURE_DESC heap_desc{0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED};
  auto heap_root = root_signature(device.Get(), heap_desc);
  if (!expect(global && local && local_constants && heap_root, "root signatures"))
    return verdict();

  ComPtr<ID3D12CommandQueue> queue;
  D3D12_COMMAND_QUEUE_DESC queue_desc{D3D12_COMMAND_LIST_TYPE_DIRECT};
  CHECK(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)));
  D3D12_DESCRIPTOR_HEAP_DESC descriptors_desc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1,
                                              D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
  ComPtr<ID3D12DescriptorHeap> descriptors;
  CHECK(device->CreateDescriptorHeap(&descriptors_desc, IID_PPV_ARGS(&descriptors)));

  // runs the export over `count` threads; every thread i must leave i + 100
  const UINT count = 64;
  auto dispatch = [&](ID3D12StateObject *object, const WCHAR *name, bool bindless) {
    ComPtr<ID3D12StateObjectProperties> properties;
    if (FAILED(object->QueryInterface(IID_PPV_ARGS(&properties))))
      return expect(false, "no properties");
    auto identifier = properties->GetShaderIdentifier(name);
    if (!expect(!!identifier, "no identifier"))
      return false;
    const UINT stride = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
    // the generation record, then a miss record that is a null identifier
    auto records = buffer(device.Get(), D3D12_HEAP_TYPE_UPLOAD, 2 * stride, D3D12_RESOURCE_STATE_GENERIC_READ);
    auto output = buffer(device.Get(), D3D12_HEAP_TYPE_DEFAULT, count * sizeof(UINT), D3D12_RESOURCE_STATE_COPY_DEST,
                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto readback = buffer(device.Get(), D3D12_HEAP_TYPE_READBACK, count * sizeof(UINT), D3D12_RESOURCE_STATE_COPY_DEST);
    auto initial = buffer(device.Get(), D3D12_HEAP_TYPE_UPLOAD, count * sizeof(UINT), D3D12_RESOURCE_STATE_GENERIC_READ);
    if (!expect(records && output && readback && initial, "dispatch buffers"))
      return false;
    void *mapped;
    if (FAILED(records->Map(0, nullptr, &mapped)))
      return expect(false, "map");
    memset(mapped, 0, 2 * stride);
    memcpy(mapped, identifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    records->Unmap(0, nullptr);
    if (FAILED(initial->Map(0, nullptr, &mapped)))
      return expect(false, "map");
    memset(mapped, 0xff, count * sizeof(UINT));
    initial->Unmap(0, nullptr);
    if (FAILED(forget(readback.Get())))
      return expect(false, "readback");
    D3D12_UNORDERED_ACCESS_VIEW_DESC view{DXGI_FORMAT_UNKNOWN, D3D12_UAV_DIMENSION_BUFFER};
    view.Buffer = {0, count, sizeof(UINT)};
    device->CreateUnorderedAccessView(output.Get(), nullptr, &view, descriptors->GetCPUDescriptorHandleForHeapStart());
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList4> list;
    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
        FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                         IID_PPV_ARGS(&list))))
      return expect(false, "command list");
    list->CopyResource(output.Get(), initial.Get());
    transition(list.Get(), output.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (bindless) {
      ID3D12DescriptorHeap *heaps[] = {descriptors.Get()};
      list->SetDescriptorHeaps(1, heaps);
      list->SetComputeRootSignature(heap_root.Get());
    } else {
      list->SetComputeRootSignature(global.Get());
      list->SetComputeRootUnorderedAccessView(0, output->GetGPUVirtualAddress());
      list->SetComputeRootShaderResourceView(1, 0);
    }
    list->SetPipelineState1(object);
    D3D12_DISPATCH_RAYS_DESC rays{};
    auto address = records->GetGPUVirtualAddress();
    rays.RayGenerationShaderRecord = {address, stride};
    rays.MissShaderTable = {address + stride, stride, stride};
    rays.Width = count;
    rays.Height = rays.Depth = 1;
    list->DispatchRays(&rays);
    transition(list.Get(), output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyResource(readback.Get(), output.Get());
    if (FAILED(submit(device.Get(), queue.Get(), list.Get())) || FAILED(readback->Map(0, nullptr, &mapped)))
      return expect(false, "submit");
    bool ok = true;
    for (UINT i = 0; i < count; i++)
      ok &= expect(((UINT *)mapped)[i] == i + 100, "thread %u wrote %u", i, ((UINT *)mapped)[i]);
    readback->Unmap(0, nullptr);
    return ok;
  };

  struct Variant {
    const char *what;
    const std::string *code;
    const WCHAR *shader;    // the library's function
    const WCHAR *exported;  // the name it is exported under, or null for a library without an export list
    UINT payload, depth;
    bool associate = false; // configs and root signatures associated with the export rather than declared
    UINT local = 0;         // 0 no local root signature, 1 an empty one, 2 one of root constants
    bool config1 = false;
    bool collection = false;
    bool configs_outside = false; // the pipeline that takes the collection declares the configs again
    bool bindless = false;
  };
  const Variant variants[] = {
      {"no export list, declared configs", &library, L"plain", nullptr, 4, 1},
      {"no export list, trace without hit groups", &library, L"traced", nullptr, 4, 1},
      {"renamed export", &library, L"plain", L"RayGen_00000001", 4, 1},
      {"renamed export, associated configs and empty local root", &library, L"plain", L"RayGen_00000002", 4, 1, true, 1},
      {"renamed export, associated local root constants", &library, L"plain", L"RayGen_00000003", 4, 1, true, 2},
      {"payload of no bytes", &library, L"plain", L"RayGen_00000004", 0, 1, true, 1},
      {"recursion depth zero", &library, L"plain", L"RayGen_00000005", 4, 0, true, 1},
      {"recursion depth zero, trace in the shader", &library, L"traced", L"RayGen_00000006", 4, 0},
      {"trace without hit groups, depth 1", &library, L"traced", L"RayGen_00000007", 4, 1, true, 1},
      {"pipeline config1 with flags", &library, L"traced", L"RayGen_00000008", 4, 1, true, 1, true},
      {"collection, configs inside", &library, L"plain", L"RayGen_00000009", 4, 1, true, 1, false, true},
      {"collection, configs inside and outside", &library, L"traced", L"RayGen_0000000a", 4, 1, true, 1, false, true,
       true},
      {"collection without export list", &library, L"traced", nullptr, 4, 1, false, 1, false, true, true},
      {"inline ray query", &query, L"queried", L"RayGen_0000000b", 4, 1, true, 1},
      {"inline ray query, no export list", &query, L"queried", nullptr, 0, 0},
      {"descriptor heap indexing", &heap, L"indexed", L"RayGen_0000000c", 4, 1, true, 1, false, false, false, true},
  };
  for (auto &v : variants) {
    step("%s", v.what);
    auto name = v.exported ? v.exported : v.shader;
    D3D12_EXPORT_DESC export_desc{v.exported, v.exported ? v.shader : nullptr};
    D3D12_DXIL_LIBRARY_DESC code{bytecode(*v.code), v.exported ? 1u : 0u, v.exported ? &export_desc : nullptr};
    D3D12_RAYTRACING_SHADER_CONFIG shader{v.payload, sizeof(float) * 2};
    D3D12_RAYTRACING_PIPELINE_CONFIG pipeline{v.depth};
    D3D12_RAYTRACING_PIPELINE_CONFIG1 pipeline1{v.depth, D3D12_RAYTRACING_PIPELINE_FLAG_SKIP_PROCEDURAL_PRIMITIVES};
    D3D12_GLOBAL_ROOT_SIGNATURE global_desc{v.bindless ? heap_root.Get() : global.Get()};
    D3D12_LOCAL_ROOT_SIGNATURE local_root{v.local == 2 ? local_constants.Get() : local.Get()};
    D3D12_STATE_OBJECT_CONFIG config{D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS};
    // reserved up front: associations point at subobjects by address
    std::vector<D3D12_STATE_SUBOBJECT> subobjects;
    subobjects.reserve(16);
    std::vector<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION> associations;
    associations.reserve(8);
    auto add = [&](D3D12_STATE_SUBOBJECT_TYPE type, const void *desc) {
      subobjects.push_back({type, desc});
      if (v.associate && type != D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY &&
          type != D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG) {
        associations.push_back({&subobjects.back(), 1, &name});
        subobjects.push_back({D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION, &associations.back()});
      }
    };
    add(D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &code);
    add(D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &shader);
    if (v.config1)
      add(D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1, &pipeline1);
    else
      add(D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pipeline);
    add(D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &global_desc);
    if (v.local)
      add(D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE, &local_root);
    add(D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG, &config);
    D3D12_STATE_OBJECT_DESC desc{v.collection ? D3D12_STATE_OBJECT_TYPE_COLLECTION
                                              : D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE,
                                 (UINT)subobjects.size(), subobjects.data()};
    ComPtr<ID3D12StateObject> object, collection;
    HRESULT hr = device->CreateStateObject(&desc, IID_PPV_ARGS(v.collection ? &collection : &object));
    if (!expect(SUCCEEDED(hr), "%s: CreateStateObject returned %08lx", v.collection ? "collection" : "pipeline", hr))
      continue;
    if (v.collection) {
      D3D12_EXISTING_COLLECTION_DESC existing{collection.Get()};
      std::vector<D3D12_STATE_SUBOBJECT> linked = {{D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION, &existing},
                                                   {D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG, &config}};
      if (v.configs_outside)
        linked.insert(linked.end(), {{D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &shader},
                                     {D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pipeline},
                                     {D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &global_desc}});
      desc = {D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE, (UINT)linked.size(), linked.data()};
      hr = device->CreateStateObject(&desc, IID_PPV_ARGS(&object));
      if (!expect(SUCCEEDED(hr), "pipeline of the collection: CreateStateObject returned %08lx", hr))
        continue;
    }
    dispatch(object.Get(), name, v.bindless);
  }
  return verdict();
}
