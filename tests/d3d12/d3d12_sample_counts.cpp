// contract: a graphics pipeline's sample count is one the device reports. "SampleDesc ... the multi-sampling
// parameters", which "must be supported", and D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS gives "the number of quality
// levels ... for a given format and sample count" (D3D12_GRAPHICS_PIPELINE_STATE_DESC and
// D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS, Microsoft Learn): with a render target or a depth target of each
// format that can be a multisampled target (D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET), a pipeline of each count
// from 1 to 32 is made exactly when the count has a quality level, as is a texture of that count. a pipeline with no
// targets rasterizes its ForcedSampleCount (D3D12_RASTERIZER_DESC: "0, 1, 4, 8, and optionally 16"), which is made
// exactly when the count is 0 or 1, or has a quality level with a format that can be a target.
#include "d3d12_test.hpp"

static const char vs_hlsl[] = "float4 main(uint id : SV_VertexID) : SV_Position { return float4(id, 0, 0, 1); }";
static const char ps_hlsl[] = "float4 main() : SV_Target { return 1; }";
static const char uav_hlsl[] = R"hlsl(
RWStructuredBuffer<uint> written : register(u0);
void main(float4 position : SV_Position) { written[0] = 1; }
)hlsl";

int
main(int argc, char **argv) {
  Compiler compiler;
  if (!front_end(argc, argv, compiler)) {
    printf("skipped: no shader compiler\n");
    return 77;
  }
  step("compile the shaders");
  auto vs = compiler.compile(vs_hlsl, "main", "vs"), ps = compiler.compile(ps_hlsl, "main", "ps"),
       uav = compiler.compile(uav_hlsl, "main", "ps");
  if (vs.empty() || ps.empty() || uav.empty()) {
    printf("failed: HLSL did not compile\n");
    return 1;
  }
  ComPtr<ID3D12Device> device;
  CHECK(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
  D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0};
  D3D12_ROOT_PARAMETER parameter{D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE};
  parameter.DescriptorTable = {1, &range};
  auto root = root_signature(device.Get(), {1, &parameter});
  if (!expect(!!root, "root signature"))
    return verdict();
  auto pipeline = [&](DXGI_FORMAT color, DXGI_FORMAT depth, UINT count, UINT forced) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = root.Get();
    desc.VS = bytecode(vs);
    desc.PS = color ? bytecode(ps) : depth ? D3D12_SHADER_BYTECODE{} : bytecode(uav);
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.ForcedSampleCount = forced;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    if (color) {
      desc.NumRenderTargets = 1;
      desc.RTVFormats[0] = color;
    }
    if (depth) {
      desc.DSVFormat = depth;
      desc.DepthStencilState.DepthEnable = TRUE;
      desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
      desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    }
    desc.SampleDesc = {count, 0};
    ComPtr<ID3D12PipelineState> made;
    return SUCCEEDED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&made)));
  };
  const DXGI_FORMAT formats[] = {
      DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_R10G10B10A2_UNORM,
      DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT,
      DXGI_FORMAT_R32G32B32A32_UINT, DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_UINT,
      DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_D32_FLOAT,
      DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
  };
  // the counts any target format has a quality level at, for a pipeline that rasterizes without one
  bool any[33] = {};
  for (auto format : formats) {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support{format};
    CHECK(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)));
    if (!(support.Support1 & D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET))
      continue;
    bool depth = support.Support1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL;
    for (UINT count = 1; count <= 32; count *= 2) {
      step("format %u, %u samples", format, count);
      D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels{format, count};
      CHECK(device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &levels, sizeof(levels)));
      bool reported = levels.NumQualityLevels > 0;
      any[count] |= reported;
      bool made = depth ? pipeline(DXGI_FORMAT_UNKNOWN, format, count, 0) : pipeline(format, DXGI_FORMAT_UNKNOWN, count, 0);
      expect(made == reported, "format %u at %u samples: %u quality levels, pipeline %s", format, count,
             levels.NumQualityLevels, made ? "made" : "refused");
      D3D12_RESOURCE_DESC texture{D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, 64, 64, 1, 1, format, {count, 0},
                                  D3D12_TEXTURE_LAYOUT_UNKNOWN,
                                  depth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET};
      D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_DEFAULT};
      ComPtr<ID3D12Resource> resource;
      bool created = SUCCEEDED(device->CreateCommittedResource(
          &heap, D3D12_HEAP_FLAG_NONE, &texture,
          depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
          IID_PPV_ARGS(&resource)
      ));
      expect(created == reported, "format %u at %u samples: %u quality levels, texture %s", format, count,
             levels.NumQualityLevels, created ? "made" : "refused");
    }
  }
  for (UINT forced : {0u, 1u, 4u, 8u, 16u}) {
    step("no targets, ForcedSampleCount %u", forced);
    bool made = pipeline(DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, 1, forced);
    expect(made == (forced <= 1 || any[forced]), "ForcedSampleCount %u: pipeline %s", forced, made ? "made" : "refused");
  }
  return verdict();
}
