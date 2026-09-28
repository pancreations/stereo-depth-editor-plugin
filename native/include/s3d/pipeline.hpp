#pragma once
#include "types.hpp"
#include <filesystem>
#include <string>

namespace s3d {
class InpaintingProvider {
public:
  virtual ~InpaintingProvider()=default;
  // Implementations must retain known pixels, preserve float color/alpha, and
  // own their temporal context by source/time identity, not callback order.
  virtual Image fill(const Image& warped,const Mask& holes,const Image& original,
                     const RepairSettings&)=0;
};
Depth process_depth(const Depth&,const DepthSettings&);
// Previous depth/image are explicit inputs; no render-order-dependent global history.
Depth stabilize(const Depth& current,const Depth& previous,const Image& guide,
                const Image& previous_guide,const TemporalSettings&,bool scene_cut=false);
float disparity(float depth,int width,const StereoSettings&);
Eye reproject(const Image&,const Depth&,const StereoSettings&,bool left);
void reconstruct(Eye&,const Image& original,const RepairSettings&,InpaintingProvider* provider=nullptr);
void content_fill(Eye&,const RepairSettings&);
Image pack_sbs(const Image& left,const Image& right,OutputMode,bool right_first=false);
Image resize(const Image&,int width,int height);
// Remove only fully transparent outer canvas margins, retaining black picture
// pixels and all partially transparent pixels. Empty frames remain unchanged.
Image trim_transparent_canvas(Image);
Image fit_canvas(const Image&,int width,int height,CanvasFit);
// Fit packed eyes independently; source_aspect is the original, unsqueezed image aspect.
Image fit_sbs_canvas(const Image&,int width,int height,OutputMode,double source_aspect,CanvasFit);
Depth resize_depth(const Depth&,int width,int height);
RenderResult render(const Image&,const Depth&,const Settings&,InpaintingProvider* provider=nullptr);
Depth read_depth_cache(const std::filesystem::path&);
Image read_pfm_image(const std::filesystem::path&);
Depth read_pfm_depth(const std::filesystem::path&);
void write_pfm(const Image&,const std::filesystem::path&);
void write_pfm(const Depth&,const std::filesystem::path&);
struct CacheBinding {
  std::string key;
  int fps_num=0,fps_den=0,frames=0,width=0,height=0;
  bool external=false;
  float low=0,high=1;
};
CacheBinding read_binding(const std::filesystem::path& root,int slot);
std::filesystem::path cache_frame_path(const std::filesystem::path& root,
                                     const CacheBinding&,std::int64_t frame);
std::int64_t frame_at_time(std::int64_t time,std::int64_t timescale,int fps_num,int fps_den);
}
