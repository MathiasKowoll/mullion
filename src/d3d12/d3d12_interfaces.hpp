#pragma once
#include "com/com_guid.hpp"
#include "d3d12.h"

// a temporal upscale on MetalFX, as nvngx's DLSS records it. the resources are in the states DLSS's are: the color,
// depth, motion and exposure readable by a compute shader, the output writable by one
struct MTL_TEMPORAL_UPSCALE_D3D12_DESC {
  UINT InputContentWidth;  // can be 0, which means the color texture's width
  UINT InputContentHeight; // can be 0, which means the color texture's height
  BOOL AutoExposure;
  BOOL InReset;
  BOOL DepthReversed;
  BOOL MotionVectorInDisplayRes;
  ID3D12Resource *Color;
  ID3D12Resource *Depth;
  ID3D12Resource *MotionVector;
  ID3D12Resource *Output;
  FLOAT MotionVectorScaleX;
  FLOAT MotionVectorScaleY;
  FLOAT PreExposure;
  ID3D12Resource *ExposureTexture; // can be null
  FLOAT JitterOffsetX;
  FLOAT JitterOffsetY;
};

// the scalers one upscale feature uses, with their history: an upscale continues from the last one of its own
DEFINE_COM_INTERFACE("2b22911c-adb4-4206-a60a-9888d71b0706", IMTLD3D12TemporalUpscaler) : public IUnknown{};

DEFINE_COM_INTERFACE("fbda471d-26f7-4aef-bf64-1ca7aa478a2c", IMTLD3D12CommandListExt) : public IUnknown {
  // whether MetalFX's temporal scaler runs on the device, and the largest output size over input content size it
  // takes there (MTLFXTemporalScalerDescriptor supportedInputContentMaxScaleForDevice:)
  virtual HRESULT STDMETHODCALLTYPE GetTemporalUpscaleSupport(BOOL *pSupported, FLOAT *pMaxScale) = 0;
  virtual HRESULT STDMETHODCALLTYPE CreateTemporalUpscaler(IMTLD3D12TemporalUpscaler **ppUpscaler) = 0;
  // records the upscale on the list, ordered after all of the list's work before it and before all after
  virtual HRESULT STDMETHODCALLTYPE
  TemporalUpscale(IMTLD3D12TemporalUpscaler *pUpscaler, const MTL_TEMPORAL_UPSCALE_D3D12_DESC *pDesc) = 0;
};
