#pragma once
#include <cstddef>
#include <cstdint>

namespace s3d::gpu {
enum class Backend { CUDA,Metal,Vulkan };
// All pointers belong to the caller on the active host device. Strides are bytes.
// No allocation, stream creation, context switching or host transfers occur here.
struct Surface {void* data;int width,height;std::size_t pitch;};
struct DepthRemap {
  float low,high,near_point,far_point,offset,scale,contrast,gamma;
  int normalize,invert,white_near,clamp;
};
struct Stereo {float strength,convergence,maximum_percent,separation_percent;int reverse;};
// CUDA stream is passed opaquely so a future backend need not include CUDA headers.
// Returns a cudaError_t numeric status. Caller must check asynchronous completion.
int cuda_remap(Surface raw,Surface depth,const DepthRemap&,void* stream);
int cuda_bilateral(Surface input,Surface output,int radius,float edge_preservation,bool vertical,void* stream);
// Requires processed finite depth, scratch z keys (uint32 per pixel), accumulator
// RGBA + weights (float), output RGBA and original visibility mask (uint8).
int cuda_warp(Surface rgba,Surface depth,Surface zkeys,Surface accum,Surface weights,
              Surface eye,Surface holes,const Stereo&,bool left,void* stream);
int cuda_repair_fast(Surface eye,Surface original,Surface holes,Surface result,int radius,void* stream);
int cuda_pack(Surface left,Surface right,Surface output,bool right_first,void* stream);
}
