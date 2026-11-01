// contract: NVAPI's GPUs are DXGI's adapters, so a game, NGX or Streamline that matches NVAPI's GPU to its Direct3D
// device by LUID finds it (MULLION_ENABLE_NVEXT=1; DXVK-NVAPI answers from the DXGI adapter the same way).
// - NvAPI_GPU_GetAdapterIdFromPhysicalGpu "returns the OS-AdapterID from physicalGpu Handle", and
//   NvAPI_GPU_GetLogicalGpuInfo fills NV_LOGICAL_GPU_DATA's pOSAdapterId: "Returns OS-AdapterId", with the logical
//   GPU's physical GPUs (NVAPI SDK, nvapi.h). The OS adapter ID is the LUID DXGI_ADAPTER_DESC::AdapterLuid holds, "a
//   unique value that identifies the adapter" (https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ns-dxgi-dxgi_adapter_desc),
//   so each GPU's LUID finds a DXGI adapter (IDXGIFactory4::EnumAdapterByLuid), adapter 0's among them.
// - a logical GPU's LUID is its physical GPU's.
// - a D3D11 device's adapter and ID3D12Device::GetAdapterLuid name the LUID of the adapter they were made on.
#include "dxgi_test.hpp"

// nvapi_QueryInterface's IDs for the functions used here (NVAPI SDK, nvapi_interface.h)
enum : unsigned {
  Initialize = 0x0150e828,
  EnumPhysicalGPUs = 0xe5ac921f,
  EnumLogicalGPUs = 0x48b3ea59,
  GetAdapterIdFromPhysicalGpu = 0x0ff07fde,
  GetLogicalGpuInfo = 0x842b066e,
};

// NV_LOGICAL_GPU_DATA_V1, version 1
struct LogicalGpuData {
  unsigned version = sizeof(LogicalGpuData) | 1 << 16;
  void *os_adapter_id;
  unsigned physical_count;
  void *physical[64];
  unsigned reserved[8];
};

int
main() {
  auto same = [](const LUID &a, const LUID &b) { return a.LowPart == b.LowPart && a.HighPart == b.HighPart; };
  ComPtr<IDXGIFactory4> factory;
  CHECK(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));
  ComPtr<IDXGIAdapter1> adapter;
  CHECK(factory->EnumAdapters1(0, &adapter));
  DXGI_ADAPTER_DESC1 desc;
  CHECK(adapter->GetDesc1(&desc));
  auto adapter_of = [&](const LUID &luid) {
    ComPtr<IDXGIAdapter1> found;
    return SUCCEEDED(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&found)));
  };

  step("loading NVAPI");
  HMODULE nvapi = LoadLibraryA("nvapi64.dll");
  auto query = nvapi ? reinterpret_cast<void *(__cdecl *)(unsigned)>((void *)GetProcAddress(nvapi, "nvapi_QueryInterface"))
                     : nullptr;
  if (!query)
    return printf("failed: no nvapi_QueryInterface in nvapi64.dll\n"), 1;
  auto initialize = reinterpret_cast<int(__cdecl *)()>(query(Initialize));
  auto enum_physical = reinterpret_cast<int(__cdecl *)(void **, unsigned *)>(query(EnumPhysicalGPUs));
  auto enum_logical = reinterpret_cast<int(__cdecl *)(void **, unsigned *)>(query(EnumLogicalGPUs));
  auto adapter_id = reinterpret_cast<int(__cdecl *)(void *, LUID *)>(query(GetAdapterIdFromPhysicalGpu));
  auto logical_info = reinterpret_cast<int(__cdecl *)(void *, LogicalGpuData *)>(query(GetLogicalGpuInfo));
  if (!initialize || !enum_physical || !enum_logical || !adapter_id || !logical_info)
    return printf("failed: NVAPI lacks a function this test uses\n"), 1;
  if (initialize())
    return printf("failed: NvAPI_Initialize, with MULLION_ENABLE_NVEXT=1\n"), 1;

  step("each physical GPU's adapter ID");
  void *physical[64] = {};
  unsigned physical_count = 0;
  bool adapter_found = false;
  expect(!enum_physical(physical, &physical_count) && physical_count, "NvAPI_EnumPhysicalGPUs gives no GPU");
  for (unsigned i = 0; i < physical_count; i++) {
    LUID luid{};
    if (!expect(!adapter_id(physical[i], &luid), "no adapter ID for physical GPU %u", i))
      continue;
    expect(adapter_of(luid), "physical GPU %u's adapter ID %08lx:%08lx is no DXGI adapter's", i, luid.HighPart,
           luid.LowPart);
    adapter_found |= same(luid, desc.AdapterLuid);
  }
  expect(adapter_found, "no physical GPU has adapter 0's LUID %08lx:%08lx", desc.AdapterLuid.HighPart,
         desc.AdapterLuid.LowPart);

  step("each logical GPU's OS adapter ID");
  void *logical[64] = {};
  unsigned logical_count = 0;
  expect(!enum_logical(logical, &logical_count) && logical_count, "NvAPI_EnumLogicalGPUs gives no GPU");
  for (unsigned i = 0; i < logical_count; i++) {
    LUID luid{}, physical_luid{};
    LogicalGpuData data{};
    data.os_adapter_id = &luid;
    if (!expect(!logical_info(logical[i], &data) && data.physical_count, "no data for logical GPU %u", i))
      continue;
    expect(adapter_of(luid), "logical GPU %u's adapter ID %08lx:%08lx is no DXGI adapter's", i, luid.HighPart,
           luid.LowPart);
    expect(!adapter_id(data.physical[0], &physical_luid) && same(luid, physical_luid),
           "logical GPU %u's adapter ID is not its physical GPU's", i);
  }

  step("the devices' adapters");
  ComPtr<ID3D11Device> d3d11;
  ComPtr<IDXGIDevice> dxgi_device;
  ComPtr<IDXGIAdapter> d3d11_adapter;
  DXGI_ADAPTER_DESC d3d11_desc{};
  CHECK(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &d3d11,
                          nullptr, nullptr));
  CHECK(d3d11.As(&dxgi_device));
  CHECK(dxgi_device->GetAdapter(&d3d11_adapter));
  CHECK(d3d11_adapter->GetDesc(&d3d11_desc));
  expect(same(d3d11_desc.AdapterLuid, desc.AdapterLuid), "the D3D11 device's adapter has another LUID");
  ComPtr<ID3D12Device> d3d12;
  CHECK(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12)));
  expect(same(d3d12->GetAdapterLuid(), desc.AdapterLuid), "ID3D12Device::GetAdapterLuid is another adapter's");
  return verdict();
}
