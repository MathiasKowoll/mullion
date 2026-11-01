// contract: an ExecuteIndirect of dispatches reads the arguments and the count a dispatch before it in the same list
// wrote, once a barrier has moved the buffer to D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT: the GPU is to "interpret the
// contents of the indirect argument buffer" when it runs, the count is "the minimum of [MaxCommandCount], and a 32-bit
// unsigned integer contained in pCountBuffer", and the commands start at ArgBufferOffset, a ByteStride apart
// (Indirect Drawing; ID3D12GraphicsCommandList::ExecuteIndirect, Microsoft Learn). the writer and every call run in one
// compute pass, as engines' GPU-driven passes record them (Unreal's DispatchIndirect is MaxCommandCount 1 without a
// count buffer), through each way the library may run a call: few commands with and without a count buffer, root
// constants per command, and more commands than it runs one by one. each group adds 1 to cell c * 16 + its x, c being
// the root constant; the buffer starts zero, so a call that read it before the writer ran adds nothing.
#include "d3d12_test.hpp"

static const char hlsl[] = R"hlsl(
cbuffer C : register(b0) { uint c; };
RWByteAddressBuffer cells : register(u0);
RWByteAddressBuffer written : register(u1);
[numthreads(1, 1, 1)]
void add(uint3 group : SV_GroupID) { cells.InterlockedAdd((c * 16 + group.x) * 4, 1); }
[numthreads(1, 1, 1)]
void write() {
  // 20 dispatches of 12 bytes from 0, (i % 3 + 1) groups each
  for (uint i = 0; i < 20; i++)
    written.Store3(i * 12, uint3(i % 3 + 1, 1, 1));
  // two counts
  written.Store2(256, uint2(2, 5));
  // 3 commands of a root constant and a dispatch from 512: constant 4 + i, i + 1 groups
  for (uint j = 0; j < 3; j++)
    written.Store4(512 + j * 16, uint4(4 + j, j + 1, 1, 1));
}
)hlsl";

int
main(int argc, char **argv) {
  Compiler compiler;
  if (!front_end(argc, argv, compiler)) {
    printf("skipped: dxcompiler.dll not found\n");
    return 77;
  }
  auto add = compiler.compile(hlsl, "add", "cs"), write = compiler.compile(hlsl, "write", "cs");
  if (add.empty() || write.empty()) {
    printf("failed: HLSL did not compile\n");
    return 1;
  }
  const UINT cells = 128;

  ComPtr<ID3D12Device> device;
  CHECK(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
  D3D12_ROOT_PARAMETER params[3] = {
      {D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS}, {D3D12_ROOT_PARAMETER_TYPE_UAV}, {D3D12_ROOT_PARAMETER_TYPE_UAV}};
  params[0].Constants = {0, 0, 1};
  params[2].Descriptor = {1, 0};
  auto rs = root_signature(device.Get(), {3, params});
  if (!rs) {
    printf("failed: root signature\n");
    return 1;
  }
  ComPtr<ID3D12PipelineState> adds, writes;
  D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc{rs.Get(), bytecode(add)};
  CHECK(device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&adds)));
  pso_desc.CS = bytecode(write);
  CHECK(device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&writes)));

  D3D12_INDIRECT_ARGUMENT_DESC dispatch{D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH};
  D3D12_COMMAND_SIGNATURE_DESC plain_desc{sizeof(D3D12_DISPATCH_ARGUMENTS), 1, &dispatch};
  ComPtr<ID3D12CommandSignature> plain, constant;
  CHECK(device->CreateCommandSignature(&plain_desc, nullptr, IID_PPV_ARGS(&plain)));
  D3D12_INDIRECT_ARGUMENT_DESC descs[2] = {{D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT}, dispatch};
  descs[0].Constant = {0, 0, 1};
  D3D12_COMMAND_SIGNATURE_DESC constant_desc{16, 2, descs};
  CHECK(device->CreateCommandSignature(&constant_desc, rs.Get(), IID_PPV_ARGS(&constant)));

  auto zeros = buffer(device.Get(), D3D12_HEAP_TYPE_UPLOAD, 1024, D3D12_RESOURCE_STATE_GENERIC_READ);
  void *mapped;
  CHECK(zeros->Map(0, nullptr, &mapped));
  memset(mapped, 0, 1024);
  auto uav = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  auto out = buffer(device.Get(), D3D12_HEAP_TYPE_DEFAULT, cells * 4, D3D12_RESOURCE_STATE_COPY_DEST, uav);
  auto written = buffer(device.Get(), D3D12_HEAP_TYPE_DEFAULT, 1024, D3D12_RESOURCE_STATE_COPY_DEST, uav);
  auto readback = buffer(device.Get(), D3D12_HEAP_TYPE_READBACK, cells * 4, D3D12_RESOURCE_STATE_COPY_DEST);
  CHECK(forget(readback.Get()));

  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  D3D12_COMMAND_QUEUE_DESC queue_desc{D3D12_COMMAND_LIST_TYPE_DIRECT};
  CHECK(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)));
  CHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
  CHECK(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), writes.Get(), IID_PPV_ARGS(&list)));
  list->CopyBufferRegion(out.Get(), 0, zeros.Get(), 0, cells * 4);
  list->CopyBufferRegion(written.Get(), 0, zeros.Get(), 0, 1024);
  transition(list.Get(), out.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  transition(list.Get(), written.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  list->SetComputeRootSignature(rs.Get());
  list->SetComputeRootUnorderedAccessView(1, out->GetGPUVirtualAddress());
  list->SetComputeRootUnorderedAccessView(2, written->GetGPUVirtualAddress());
  list->Dispatch(1, 1, 1);
  transition(list.Get(), written.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
  list->SetPipelineState(adds.Get());
  // c 0: commands 1 to 3, no count buffer
  list->SetComputeRoot32BitConstant(0, 0, 0);
  list->ExecuteIndirect(plain.Get(), 3, written.Get(), 12, nullptr, 0);
  // c 1: a count of 2 of at most 4
  list->SetComputeRoot32BitConstant(0, 1, 0);
  list->ExecuteIndirect(plain.Get(), 4, written.Get(), 0, written.Get(), 256);
  // c 2: 20 commands, no count buffer
  list->SetComputeRoot32BitConstant(0, 2, 0);
  list->ExecuteIndirect(plain.Get(), 20, written.Get(), 0, nullptr, 0);
  // c 3: a count of 5 of at most 1
  list->SetComputeRoot32BitConstant(0, 3, 0);
  list->ExecuteIndirect(plain.Get(), 1, written.Get(), 0, written.Get(), 260);
  // c from the commands: a count of 5 of at most 3
  list->ExecuteIndirect(constant.Get(), 3, written.Get(), 512, written.Get(), 260);
  transition(list.Get(), out.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
  list->CopyResource(readback.Get(), out.Get());
  HRESULT ran = submit(device.Get(), queue.Get(), list.Get());
  if (FAILED(ran)) {
    printf("failed: the list: %08lx\n", ran);
    return 1;
  }

  UINT want[cells] = {};
  auto groups = [&](UINT c, UINT x) {
    for (UINT g = 0; g < x; g++)
      want[c * 16 + g]++;
  };
  for (UINT i = 1; i <= 3; i++)
    groups(0, i % 3 + 1);
  for (UINT i = 0; i < 2; i++)
    groups(1, i % 3 + 1);
  for (UINT i = 0; i < 20; i++)
    groups(2, i % 3 + 1);
  groups(3, 1);
  for (UINT j = 0; j < 3; j++)
    groups(4 + j, j + 1);
  UINT *got;
  CHECK(readback->Map(0, nullptr, (void **)&got));
  unsigned mismatches = 0;
  for (UINT i = 0; i < cells; i++)
    if (got[i] != want[i] && ++mismatches <= 16)
      printf("cell %u: got %u, want %u\n", i, got[i], want[i]);
  printf("%s: %u mismatching cells of %u\n", mismatches ? "failed" : "passed", mismatches, cells);
  return mismatches != 0;
}
