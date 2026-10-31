#pragma once
#include "nvngx.hpp"
#include "../d3d12/d3d12_interfaces.hpp"
#include <cstdint>

namespace mullion {

struct CommonFeature {
  unsigned int handle;
  NVNGX_FEATURE feature;
};

struct DLSSFeature : CommonFeature {
  uint32_t width;
  uint32_t height;
  uint32_t target_width;
  uint32_t target_height;
  int quality;
  int flag;
  int enable_output_subrects;
};

// a D3D12 feature's MetalFX scalers are the list's device's, made when the feature is
struct DLSSFeatureD3D12 : DLSSFeature {
  IMTLD3D12TemporalUpscaler *upscaler = nullptr;
};

}