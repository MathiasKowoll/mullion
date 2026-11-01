// the workload tests/held.py profiles for the frame verdict: phases of frames whose GPU idles for a known reason, each
// as a 'fact phase <first> <last> <name>' line. argv: <front end> <frames a phase>.
// cpu: the program sleeps 10 ms before it submits a frame of a clear, so the GPU idles while the application works.
// gpu: the direct queue runs a compute dispatch of about 20 ms each frame, two frames in flight: the GPU never idles.
// overlap: a compute queue runs a dispatch of about 12 ms while the direct queue runs another, and the direct queue
// waits for it before it presents: the two queues overlap by about the shorter one.
// gate: the direct queue waits for the compute queue's fence before its frame, and the program sleeps 8 ms before
// it submits the compute work that signals it: the direct queue's work is held back behind the Wait.
// submit: 384 lists of a small dispatch each, each submitted alone: the GPU idles while the CPU records and submits.
// each frame's length from Present to Present, as the program measures it, is a 'fact frame <frame> <ns>' line, which
// a run without the profile compares with to measure what the profile costs.
// the dispatch lengths come from timestamp queries of a dispatch timed first ('fact turns <ns of 65536 turns>'), its
// fastest of several, as the GPU's clock rises under the first: a dispatch then lasts at least its time, and a frame of
// one stays longer than the display's, which would otherwise hold it as a wait for a drawable.
#include "d3d12_test.hpp"
#include <dxgi1_4.h>

static const char hlsl[] = R"hlsl(
cbuffer turns : register(b0) { uint turns; };
RWStructuredBuffer<uint> output : register(u0);
[numthreads(64, 1, 1)]
void cs(uint id : SV_DispatchThreadID) {
  uint value = id;
  [loop] for (uint i = 0; i < turns; i++) value = value * 1664525u + 1013904223u;
  output[id] = value;
}
)hlsl";

static constexpr UINT size = 64, buffers = 3, groups = 64, threads = groups * 64;
static constexpr DWORD cpu_ms = 10, gate_ms = 8;
static constexpr UINT small_lists = 384;
static const char *const phases[] = {"cpu", "gpu", "overlap", "gate", "submit"};

int
main(int argc, char **argv) {
  Compiler compiler;
  if (!front_end(argc, argv, compiler)) {
    printf("skipped: dxcompiler.dll not found\n");
    return 77;
  }
  const UINT each = argc > 2 ? atoi(argv[2]) : 24;
  auto cs = compiler.compile(hlsl, "cs", "cs");
  if (cs.empty()) {
    printf("failed: HLSL did not compile\n");
    return 1;
  }
  WNDCLASSA wc{0, DefWindowProcA, 0, 0, GetModuleHandleA(nullptr), nullptr, nullptr, nullptr, nullptr, "bounds"};
  RegisterClassA(&wc);
  auto hwnd = CreateWindowA("bounds", "bounds", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, size, size, nullptr, nullptr,
                            wc.hInstance, nullptr);
  ComPtr<ID3D12Device> device;
  CHECK(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
  D3D12_ROOT_PARAMETER params[2]{};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[0].Constants = {0, 0, 1};
  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  params[1].Descriptor = {0, 0};
  auto rs = root_signature(device.Get(), {2, params});
  D3D12_COMPUTE_PIPELINE_STATE_DESC cs_desc{rs.Get(), bytecode(cs)};
  ComPtr<ID3D12PipelineState> pso;
  CHECK(device->CreateComputePipelineState(&cs_desc, IID_PPV_ARGS(&pso)));
  ComPtr<ID3D12Resource> uav[2];
  for (auto &target : uav)
    target = buffer(device.Get(), D3D12_HEAP_TYPE_DEFAULT, threads * 4, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

  ComPtr<ID3D12CommandQueue> direct, compute;
  D3D12_COMMAND_QUEUE_DESC queue_desc{D3D12_COMMAND_LIST_TYPE_DIRECT};
  CHECK(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&direct)));
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
  CHECK(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&compute)));
  ComPtr<IDXGIFactory4> factory;
  CHECK(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
  DXGI_SWAP_CHAIN_DESC1 sc_desc{size, size, DXGI_FORMAT_R8G8B8A8_UNORM, FALSE, {1, 0}, DXGI_USAGE_RENDER_TARGET_OUTPUT,
                                buffers};
  sc_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  ComPtr<IDXGISwapChain1> sc1;
  CHECK(factory->CreateSwapChainForHwnd(direct.Get(), hwnd, &sc_desc, nullptr, nullptr, &sc1));
  ComPtr<IDXGISwapChain3> swapchain;
  CHECK(sc1.As(&swapchain));
  ComPtr<ID3D12DescriptorHeap> rtv_heap;
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, buffers};
  CHECK(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&rtv_heap)));
  auto rtv_step = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  ComPtr<ID3D12Resource> back[buffers];
  for (UINT i = 0; i < buffers; i++) {
    CHECK(swapchain->GetBuffer(i, IID_PPV_ARGS(&back[i])));
    device->CreateRenderTargetView(back[i].Get(), nullptr, {rtv_heap->GetCPUDescriptorHandleForHeapStart().ptr + i * rtv_step});
  }

  // two frames in flight, each with its own allocators
  ComPtr<ID3D12CommandAllocator> allocators[2][2];
  ComPtr<ID3D12GraphicsCommandList> lists[2];
  const D3D12_COMMAND_LIST_TYPE types[2] = {D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_TYPE_COMPUTE};
  for (UINT q = 0; q < 2; q++) {
    for (auto &allocator : allocators[q])
      CHECK(device->CreateCommandAllocator(types[q], IID_PPV_ARGS(&allocator)));
    CHECK(device->CreateCommandList(0, types[q], allocators[q][0].Get(), nullptr, IID_PPV_ARGS(&lists[q])));
    CHECK(lists[q]->Close());
  }
  // the submit phase's lists, for each of the two frames in flight
  ComPtr<ID3D12CommandAllocator> small_allocators[2][small_lists];
  ComPtr<ID3D12GraphicsCommandList> small[small_lists];
  for (UINT n = 0; n < small_lists; n++) {
    for (auto &allocator : small_allocators)
      CHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator[n])));
    CHECK(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, small_allocators[0][n].Get(), nullptr,
                                    IID_PPV_ARGS(&small[n])));
    CHECK(small[n]->Close());
  }
  ComPtr<ID3D12Fence> frame_fence, compute_fence;
  CHECK(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&frame_fence)));
  CHECK(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&compute_fence)));
  auto event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  auto dispatch = [&](ID3D12GraphicsCommandList *list, UINT turns, UINT target) {
    list->SetPipelineState(pso.Get());
    list->SetComputeRootSignature(rs.Get());
    list->SetComputeRoot32BitConstant(0, turns, 0);
    list->SetComputeRootUnorderedAccessView(1, uav[target]->GetGPUVirtualAddress());
    list->Dispatch(groups, 1, 1);
  };

  // how long 65536 turns take, from a dispatch's timestamps
  ComPtr<ID3D12QueryHeap> timestamps;
  D3D12_QUERY_HEAP_DESC query_desc{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 2};
  CHECK(device->CreateQueryHeap(&query_desc, IID_PPV_ARGS(&timestamps)));
  auto readback = buffer(device.Get(), D3D12_HEAP_TYPE_READBACK, 16, D3D12_RESOURCE_STATE_COPY_DEST);
  UINT64 ticks;
  CHECK(direct->GetTimestampFrequency(&ticks));
  UINT64 unit = ~0ull;
  for (UINT run = 0; run < 8; run++) {
    CHECK(allocators[0][0]->Reset());
    CHECK(lists[0]->Reset(allocators[0][0].Get(), nullptr));
    lists[0]->EndQuery(timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    dispatch(lists[0].Get(), 1 << 18, 0);
    lists[0]->EndQuery(timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
    lists[0]->ResolveQueryData(timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, readback.Get(), 0);
    CHECK(submit(device.Get(), direct.Get(), lists[0].Get()));
    UINT64 *clock;
    CHECK(readback->Map(0, nullptr, (void **)&clock));
    unit = std::min<UINT64>(unit, (clock[1] - clock[0]) * 1000000000 / ticks / 4);
    readback->Unmap(0, nullptr);
  }
  if (!unit || unit == ~0ull) {
    printf("failed: the timed dispatch took no time\n");
    return 1;
  }
  printf("fact turns %llu\n", unit);
  auto turns = [&](UINT64 ms) { return UINT(std::min<UINT64>(ms * 1000000 * 65536 / unit, 1u << 30)); };

  LARGE_INTEGER qpc;
  QueryPerformanceFrequency(&qpc);
  auto now = [&] {
    LARGE_INTEGER time;
    QueryPerformanceCounter(&time);
    return time.QuadPart / qpc.QuadPart * 1000000000 + time.QuadPart % qpc.QuadPart * 1000000000 / qpc.QuadPart;
  };
  auto presented = now();
  UINT64 compute_value = 0;
  const UINT frames = each * UINT(std::size(phases));
  for (UINT f = 0; f < frames; f++) {
    std::string phase = phases[f / each];
    if (f % each == 0)
      printf("fact phase %u %u %s\n", f, f + each - 1, phase.c_str());
    // the frame before the last has completed, so its allocators are free
    if (f >= 2 && frame_fence->GetCompletedValue() < f - 1) {
      CHECK(frame_fence->SetEventOnCompletion(f - 1, event));
      WaitForSingleObject(event, INFINITE);
    }
    if (phase == "cpu")
      Sleep(cpu_ms);
    auto i = swapchain->GetCurrentBackBufferIndex();
    auto list = lists[0].Get();
    CHECK(allocators[0][f % 2]->Reset());
    CHECK(list->Reset(allocators[0][f % 2].Get(), nullptr));
    transition(list, back[i].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{rtv_heap->GetCPUDescriptorHandleForHeapStart().ptr + i * rtv_step};
    const float color[4] = {float(f % 7) / 7, 0, 0, 1};
    list->ClearRenderTargetView(rtv, color, 0, nullptr);
    if (phase == "gpu" || phase == "overlap")
      dispatch(list, turns(phase == "gpu" ? 20 : 12), 0);
    transition(list, back[i].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    CHECK(list->Close());
    auto compute_work = [&](UINT64 ms) -> HRESULT {
      auto list = lists[1].Get();
      HRESULT hr;
      if (FAILED(hr = allocators[1][f % 2]->Reset()) || FAILED(hr = list->Reset(allocators[1][f % 2].Get(), nullptr)))
        return hr;
      dispatch(list, turns(ms), 1);
      if (FAILED(hr = list->Close()))
        return hr;
      ID3D12CommandList *run[] = {list};
      compute->ExecuteCommandLists(1, run);
      return compute->Signal(compute_fence.Get(), ++compute_value);
    };
    ID3D12CommandList *run[] = {list};
    if (phase == "submit")
      for (UINT n = 0; n < small_lists; n++) {
        auto &allocator = small_allocators[f % 2][n];
        CHECK(allocator->Reset());
        CHECK(small[n]->Reset(allocator.Get(), nullptr));
        dispatch(small[n].Get(), 1, 0);
        CHECK(small[n]->Close());
        ID3D12CommandList *one[] = {small[n].Get()};
        direct->ExecuteCommandLists(1, one);
      }
    if (phase == "overlap") {
      CHECK(compute_work(12));
      direct->ExecuteCommandLists(1, run);
      CHECK(direct->Wait(compute_fence.Get(), compute_value));
    } else if (phase == "gate") {
      CHECK(direct->Wait(compute_fence.Get(), compute_value + 1));
      direct->ExecuteCommandLists(1, run);
      Sleep(gate_ms);
      CHECK(compute_work(1));
    } else {
      direct->ExecuteCommandLists(1, run);
    }
    CHECK(swapchain->Present(0, 0));
    CHECK(direct->Signal(frame_fence.Get(), f + 1));
    auto presenting = now();
    printf("fact frame %u %lld\n", f, presenting - presented);
    presented = presenting;
    MSG msg;
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE))
      DispatchMessageA(&msg);
  }
  CHECK(frame_fence->SetEventOnCompletion(frames, event));
  WaitForSingleObject(event, INFINITE);
  printf("passed: %u frames\n", frames);
  return 0;
}
