// a frame of many passes, as an engine's is: compute passes of four pipelines of different cost, each followed by a
// copy or a draw of its own pass, every one in an event of its own; or, with "chained", the dispatches alone with a UAV
// barrier between each two, as an engine's compute passes follow each other. argv: <front end> <frames>
// [dispatches] [mixed|chained]. the
// program measures each frame's GPU time with timestamp queries at its start and end ('fact gpu <frame> <ns>') and
// says how many passes of each kind a frame has ('fact passes <compute> <copies> <draws>')
#include "d3d12_test.hpp"
#include <dxgi1_4.h>

static const char hlsl[] = R"hlsl(
#ifndef TURNS
#define TURNS 1
#endif
RWStructuredBuffer<float> data : register(u0);
[numthreads(256, 1, 1)] void cs(uint id : SV_DispatchThreadID) {
  float x = data[id];
  [loop] for (uint i = 0; i < TURNS; i++) x = x * 1.0001f + 0.5f;
  data[id] = x;
}
float4 vs(uint id : SV_VertexID) : SV_Position {
  float2 uv = float2((id << 1) & 2, id & 2);
  return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
float4 ps(float4 pos : SV_Position) : SV_Target {
  float x = pos.x;
  [loop] for (uint i = 0; i < TURNS; i++) x = x * 1.0001f + 0.5f;
  return float4(frac(x), 0, 0, 1);
}
)hlsl";

static constexpr UINT size = 64, buffers = 2, threads = 1 << 20, target = 1024, copied = 4 << 20;
static constexpr UINT turns[] = {32, 128, 512, 1024};

int
main(int argc, char **argv) {
  Compiler compiler;
  if (!front_end(argc, argv, compiler)) {
    printf("skipped: dxcompiler.dll not found\n");
    return 77;
  }
  const UINT frames = argc > 2 ? atoi(argv[2]) : 60, dispatches = argc > 3 ? atoi(argv[3]) : 120;
  const bool chained = argc > 4 && !strcmp(argv[4], "chained");
  std::vector<std::string> cs;
  for (auto n : turns)
    cs.push_back(compiler.compile(hlsl, "cs", "cs", {"TURNS=" + std::to_string(n)}));
  auto vs = compiler.compile(hlsl, "vs", "vs"), ps = compiler.compile(hlsl, "ps", "ps", {"TURNS=256"});
  for (auto &code : cs)
    if (code.empty() || vs.empty() || ps.empty()) {
      printf("failed: HLSL did not compile\n");
      return 1;
    }

  WNDCLASSA wc{0, DefWindowProcA, 0, 0, GetModuleHandleA(nullptr), nullptr, nullptr, nullptr, nullptr, "passes"};
  RegisterClassA(&wc);
  auto hwnd = CreateWindowA("passes", "passes", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, size, size, nullptr, nullptr,
                            wc.hInstance, nullptr);
  ComPtr<ID3D12Device> device;
  CHECK(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
  D3D12_ROOT_PARAMETER uav{D3D12_ROOT_PARAMETER_TYPE_UAV};
  uav.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  auto compute_rs = root_signature(device.Get(), {1, &uav});
  auto graphics_rs = root_signature(device.Get(), {});
  std::vector<ComPtr<ID3D12PipelineState>> pipelines(cs.size());
  for (size_t i = 0; i < cs.size(); i++) {
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc{compute_rs.Get(), bytecode(cs[i])};
    CHECK(device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipelines[i])));
  }
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
  desc.pRootSignature = graphics_rs.Get();
  desc.VS = bytecode(vs);
  desc.PS = bytecode(ps);
  desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  desc.SampleMask = ~0u;
  desc.RasterizerState = {D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE};
  desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  desc.NumRenderTargets = 1;
  desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc = {1, 0};
  ComPtr<ID3D12PipelineState> draw;
  CHECK(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&draw)));

  ComPtr<ID3D12CommandQueue> queue;
  D3D12_COMMAND_QUEUE_DESC queue_desc{D3D12_COMMAND_LIST_TYPE_DIRECT};
  CHECK(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)));
  ComPtr<IDXGIFactory4> factory;
  CHECK(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
  DXGI_SWAP_CHAIN_DESC1 sc_desc{size, size, DXGI_FORMAT_R8G8B8A8_UNORM, FALSE, {1, 0}, DXGI_USAGE_RENDER_TARGET_OUTPUT,
                                buffers};
  sc_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  ComPtr<IDXGISwapChain1> sc1;
  CHECK(factory->CreateSwapChainForHwnd(queue.Get(), hwnd, &sc_desc, nullptr, nullptr, &sc1));
  ComPtr<IDXGISwapChain3> swapchain;
  CHECK(sc1.As(&swapchain));

  auto data = buffer(device.Get(), D3D12_HEAP_TYPE_DEFAULT, threads * sizeof(float), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                     D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  auto from = buffer(device.Get(), D3D12_HEAP_TYPE_DEFAULT, copied, D3D12_RESOURCE_STATE_COPY_SOURCE);
  auto to = buffer(device.Get(), D3D12_HEAP_TYPE_DEFAULT, copied, D3D12_RESOURCE_STATE_COPY_DEST);
  D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_DEFAULT};
  D3D12_RESOURCE_DESC tex{D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, target, target, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0},
                          D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET};
  ComPtr<ID3D12Resource> rt;
  CHECK(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &tex, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                        IID_PPV_ARGS(&rt)));
  ComPtr<ID3D12DescriptorHeap> rtv_heap;
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, buffers + 1};
  CHECK(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&rtv_heap)));
  auto rtv_step = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  auto rtv = [&](UINT i) { return D3D12_CPU_DESCRIPTOR_HANDLE{rtv_heap->GetCPUDescriptorHandleForHeapStart().ptr + i * rtv_step}; };
  ComPtr<ID3D12Resource> back[buffers];
  for (UINT i = 0; i < buffers; i++) {
    CHECK(swapchain->GetBuffer(i, IID_PPV_ARGS(&back[i])));
    device->CreateRenderTargetView(back[i].Get(), nullptr, rtv(i));
  }
  device->CreateRenderTargetView(rt.Get(), nullptr, rtv(buffers));
  ComPtr<ID3D12QueryHeap> timestamps;
  D3D12_QUERY_HEAP_DESC query_desc{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 2};
  CHECK(device->CreateQueryHeap(&query_desc, IID_PPV_ARGS(&timestamps)));
  auto readback = buffer(device.Get(), D3D12_HEAP_TYPE_READBACK, 2 * sizeof(UINT64), D3D12_RESOURCE_STATE_COPY_DEST);
  UINT64 ticks;
  CHECK(queue->GetTimestampFrequency(&ticks));

  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  CHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
  CHECK(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)));
  CHECK(list->Close());
  auto event = [&](const std::string &name) { list->BeginEvent(1, name.c_str(), UINT(name.size() + 1)); };
  printf("fact passes %u %u %u\n", dispatches, chained ? 0 : (dispatches + 1) / 2, chained ? 0 : dispatches / 2);
  for (UINT f = 0; f < frames; f++) {
    auto i = swapchain->GetCurrentBackBufferIndex();
    CHECK(allocator->Reset());
    CHECK(list->Reset(allocator.Get(), nullptr));
    list->EndQuery(timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    D3D12_VIEWPORT viewport{0, 0, (float)target, (float)target, 0, 1};
    D3D12_RECT scissor{0, 0, (LONG)target, (LONG)target};
    for (UINT d = 0; d < dispatches; d++) {
      auto kind = d % std::size(turns);
      event("dispatch " + std::to_string(d) + " of " + std::to_string(turns[kind]));
      list->SetComputeRootSignature(compute_rs.Get());
      list->SetPipelineState(pipelines[kind].Get());
      list->SetComputeRootUnorderedAccessView(0, data->GetGPUVirtualAddress());
      list->Dispatch(threads / 256, 1, 1);
      list->EndEvent();
      if (chained) {
        D3D12_RESOURCE_BARRIER barrier{D3D12_RESOURCE_BARRIER_TYPE_UAV};
        barrier.UAV.pResource = data.Get();
        list->ResourceBarrier(1, &barrier);
      } else if (d % 2 == 0) {
        event("copy " + std::to_string(d));
        list->CopyBufferRegion(to.Get(), 0, from.Get(), 0, copied);
        list->EndEvent();
      } else {
        event("draw " + std::to_string(d));
        auto target_rtv = rtv(buffers);
        list->OMSetRenderTargets(1, &target_rtv, FALSE, nullptr);
        list->SetGraphicsRootSignature(graphics_rs.Get());
        list->SetPipelineState(draw.Get());
        list->RSSetViewports(1, &viewport);
        list->RSSetScissorRects(1, &scissor);
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        list->DrawInstanced(3, 1, 0, 0);
        list->EndEvent();
      }
    }
    list->EndQuery(timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
    list->ResolveQueryData(timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, readback.Get(), 0);
    transition(list.Get(), back[i].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    float color[] = {0.1f, 0.2f, 0.3f, 1};
    list->ClearRenderTargetView(rtv(i), color, 0, nullptr);
    transition(list.Get(), back[i].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    CHECK(submit(device.Get(), queue.Get(), list.Get()));
    UINT64 *clock;
    CHECK(readback->Map(0, nullptr, (void **)&clock));
    printf("fact gpu %u %lld\n", f, (long long)((clock[1] - clock[0]) * 1000000000 / ticks));
    readback->Unmap(0, nullptr);
    CHECK(swapchain->Present(0, 0));
    MSG msg;
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE))
      DispatchMessageA(&msg);
  }
  printf("passed: %u frames\n", frames);
  return 0;
}
