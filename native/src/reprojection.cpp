#include "s3d/pipeline.hpp"

namespace s3d {
float disparity(float d,int width,const StereoSettings& s) {
  const float limit=s.max_parallax_percent*0.01f*width;
  const float v=(d-s.convergence)*s.strength*s.eye_separation_percent*0.01f*width;
  return std::clamp(v,-limit,limit)*(s.reverse_depth?-1.0f:1.0f);
}
Eye reproject(const Image& src,const Depth& d,const StereoSettings& s,bool left) {
  src.validate();d.validate();
  if(src.width!=d.width || src.height!=d.height) throw std::invalid_argument("Depth/image dimensions differ");
  for(float v:{s.strength,s.convergence,s.eye_separation_percent,s.max_parallax_percent}) finite(v);
  if(s.strength<0 || s.strength>100 || s.eye_separation_percent<0 || s.eye_separation_percent>100 ||
     s.max_parallax_percent<0 || s.max_parallax_percent>100) throw std::invalid_argument("Invalid stereo settings");
  Eye eye{Image(src.width,src.height),Depth(src.width,src.height,-std::numeric_limits<float>::infinity()),Mask(src.width,src.height,1)};
  Depth weights(src.width,src.height);
  for(int y=0;y<src.height;++y) for(int x=0;x<src.width;++x) {
    float z=d.at(x,y);
    if(!std::isfinite(z)) throw std::invalid_argument("Non-finite processed depth");
    // Near objects: x_left > x_right (crossed/negative screen parallax).
    float dest=x+(left?0.5f:-0.5f)*disparity(z,src.width,s);
    int start=int(std::floor(dest));
    for(int k=0;k<2;++k) {
      int dx=start+k;
      float w=k?dest-start:1-(dest-start);
      if(dx<0 || dx>=src.width || w<1e-6f) continue;
      float& oldz=eye.z.at(dx,y);float& total=weights.at(dx,y);
      if(z>oldz+1e-5f) {oldz=z;total=0;eye.image.at(dx,y)={};}
      if(std::abs(z-oldz)<=1e-5f) {
        eye.image.at(dx,y)=eye.image.at(dx,y)+src.at(x,y)*w;
        total+=w;eye.holes.at(dx,y)=0;
      }
    }
  }
  for(std::size_t i=0;i<weights.pixels.size();++i)
    if(weights.pixels[i]>0) eye.image.pixels[i]=eye.image.pixels[i]*(1/weights.pixels[i]);
  return eye;
}
}
