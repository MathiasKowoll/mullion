#pragma once

#include "Metal.hpp"
#include <atomic>

namespace mullion {

class SpatialScaler {
public:
  void incRef();
  void decRef();

  SpatialScaler(WMT::Device device, const WMTFXSpatialScalerInfo &info);

  WMT::FXSpatialScaler scaler() {
    return scaler_;
  }

  WMT::Fence fence() {
    return fence_;
  }

private:
  WMT::Reference<WMT::FXSpatialScaler> scaler_;
  WMT::Reference<WMT::Fence> fence_;
  std::atomic<uint32_t> refcount_;
};

class TemporalScaler {
public:
  void incRef();
  void decRef();

  // a Metal 4 scaler encodes into a Metal 4 command buffer (MTL4FXTemporalScaler), a classic one into a classic one
  TemporalScaler(WMT::Device device, const WMTFXTemporalScalerInfo &info, bool metal4 = false);

  WMT::FXTemporalScaler scaler() {
    return scaler_;
  }

  WMT::Fence fence() {
    return fence_;
  }

private:
  WMT::Reference<WMT::FXTemporalScaler> scaler_;
  WMT::Reference<WMT::Fence> fence_;
  std::atomic<uint32_t> refcount_;
};

} // namespace mullion