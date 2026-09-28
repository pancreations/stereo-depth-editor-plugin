#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace s3d {
// Color is opaque to the algorithm: float RGBA, including negative/HDR values.
// Host adapters retain primaries/transfer/alpha metadata and convert channel order only.
struct Pixel { float r=0, g=0, b=0, a=0; };
inline Pixel operator+(Pixel a, Pixel b) { return {a.r+b.r,a.g+b.g,a.b+b.b,a.a+b.a}; }
inline Pixel operator*(Pixel a, float f) { return {a.r*f,a.g*f,a.b*f,a.a*f}; }
inline Pixel mix(Pixel a, Pixel b, float t) { return a*(1-t)+b*t; }
inline std::size_t checked_size(int w,int h) {
  if(w<=0 || h<=0 || w>32768 || h>32768 || std::uint64_t(w)*h>268435456)
    throw std::invalid_argument("Invalid or excessive image dimensions");
  return std::size_t(w)*h;
}
template<class T> struct Plane {
  int width=0,height=0;
  std::vector<T> pixels;
  Plane()=default;
  Plane(int w,int h,T fill={}):width(w),height(h),pixels(checked_size(w,h),fill){}
  T& at(int x,int y) { return pixels[std::size_t(y)*width+x]; }
  const T& at(int x,int y) const { return pixels[std::size_t(y)*width+x]; }
  void validate() const { if(pixels.size()!=checked_size(width,height)) throw std::invalid_argument("Invalid plane storage"); }
};
using Image=Plane<Pixel>;
using Depth=Plane<float>;
using Mask=Plane<std::uint8_t>;
inline float clamp01(float v) { return std::clamp(v,0.0f,1.0f); }
inline void finite(float v) { if(!std::isfinite(v)) throw std::invalid_argument("Non-finite setting"); }
struct DepthSettings {
  float fidelity=1, contrast=1, gamma=1, near_point=1, far_point=0;
  float offset=0, scale=1, blur=0, edge_preservation=0.8f;
  bool clamp=true, normalize=false, invert=false, white_near=true;
  // Fixed shot/sequence range; never calculate independent min/max each frame.
  float normalization_low=0,normalization_high=1;
};
struct TemporalSettings {
  bool enabled=false;
  float strength=0.6f,motion_sensitivity=0.7f,edge_preservation=0.8f;
};
struct StereoSettings {
  float strength=1,convergence=0.5f,max_parallax_percent=3,eye_separation_percent=2;
  bool reverse_depth=false;
};
enum class Reconstruction { Off,Fast,High,AI };
struct RepairSettings {
  Reconstruction mode=Reconstruction::Fast;
  int radius=32;
  float softness=0.2f,artifact_reduction=0.7f;
};
enum class OutputMode { HalfSBS,FullSBS };
enum class CanvasFit { Fit,Fill };
enum class Preview { Final,Original,DepthMap,Left,Right,Difference,Parallax,Occlusion };
enum class Quality { Preview,Balanced,High,Maximum };
struct Settings {
  DepthSettings depth;
  StereoSettings stereo;
  RepairSettings repair;
  OutputMode output=OutputMode::HalfSBS;
  Preview preview=Preview::Final;
  bool right_first=false;
};
struct Eye { Image image; Depth z; Mask holes; };
struct RenderResult { Image output; Depth processed_depth; Mask left_holes,right_holes; };
}
