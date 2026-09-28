#pragma once
#include "s3d/pipeline.hpp"
namespace s3d::gpu {
// CUDA accelerated CPU-frame adapter. Host transfers remain until PrGPUFilter integration.
Image render_cuda(const Image&,const Depth&,const Settings&);
}
