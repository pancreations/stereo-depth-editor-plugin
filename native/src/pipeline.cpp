#include "s3d/pipeline.hpp"

namespace s3d {
RenderResult render(const Image& src,const Depth& raw,const Settings& s,InpaintingProvider* provider) {
  src.validate();raw.validate();
  Depth d=process_depth(raw,s.depth);
  if(d.width!=src.width || d.height!=src.height) d=resize_depth(d,src.width,src.height);
  if(s.preview==Preview::Original) return {src,std::move(d),{}, {}};
  if(s.preview==Preview::DepthMap || s.preview==Preview::Parallax) {
    Image out(src.width,src.height);
    for(std::size_t i=0;i<out.pixels.size();++i) {
      float v=d.pixels[i];
      if(s.preview==Preview::DepthMap) out.pixels[i]={v,v,v,1};
      else {
        float limit=std::max(1e-6f,s.stereo.max_parallax_percent*src.width*0.01f);
        float p=disparity(v,src.width,s.stereo)/limit;
        out.pixels[i]={std::max(p,0.0f),1-std::abs(p),std::max(-p,0.0f),1};
      }
    }
    return {std::move(out),std::move(d),{}, {}};
  }
  Eye left=reproject(src,d,s.stereo,true),right=reproject(src,d,s.stereo,false);
  reconstruct(left,src,s.repair,provider);reconstruct(right,src,s.repair,provider);
  Image out;
  if(s.preview==Preview::Left) out=std::move(left.image);
  else if(s.preview==Preview::Right) out=std::move(right.image);
  else if(s.preview==Preview::Difference || s.preview==Preview::Occlusion) {
    out=Image(src.width,src.height);
    for(std::size_t i=0;i<out.pixels.size();++i) {
      Pixel a=left.image.pixels[i],b=right.image.pixels[i];
      out.pixels[i]=s.preview==Preview::Difference?Pixel{std::abs(a.r-b.r),std::abs(a.g-b.g),std::abs(a.b-b.b),1}:
        Pixel{float(left.holes.pixels[i]),0,float(right.holes.pixels[i]),1};
    }
  } else out=pack_sbs(left.image,right.image,s.output,s.right_first);
  return {std::move(out),std::move(d),std::move(left.holes),std::move(right.holes)};
}
}
