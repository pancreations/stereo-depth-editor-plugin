#include "s3d/pipeline.hpp"

namespace s3d {
Depth process_depth(const Depth& input,const DepthSettings& s) {
  input.validate();
  for(float v:{s.fidelity,s.contrast,s.gamma,s.near_point,s.far_point,s.offset,s.scale,
      s.blur,s.edge_preservation,s.normalization_low,s.normalization_high}) finite(v);
  if(s.gamma<=0 || s.contrast<0 || s.fidelity<0 || s.fidelity>1 || s.blur<0 ||
     s.blur>16 || s.edge_preservation<0 || s.edge_preservation>1 ||
     std::abs(s.near_point-s.far_point)<1e-6f)
    throw std::invalid_argument("Invalid depth remap settings");
  if(s.normalize && s.normalization_high<=s.normalization_low)
    throw std::invalid_argument("Normalization needs a fixed increasing range");
  Depth d(input.width,input.height);
  for(std::size_t i=0;i<d.pixels.size();++i) {
    float v=input.pixels[i];
    if(!std::isfinite(v)) v=s.far_point;
    if(s.normalize) v=(v-s.normalization_low)/(s.normalization_high-s.normalization_low);
    if(!s.white_near) v=1-v;
    v=(v-s.far_point)/(s.near_point-s.far_point);
    if(s.invert) v=1-v;
    v=(v-0.5f)*s.scale+0.5f+s.offset;
    v=(v-0.5f)*s.contrast+0.5f;
    // Signed power preserves out-of-range depth when Clamp Depth is off.
    v=std::copysign(std::pow(std::abs(v),1/s.gamma),v);
    if(!std::isfinite(v)) throw std::invalid_argument("Depth remap overflow");
    d.pixels[i]=s.clamp?clamp01(v):v;
  }
  const int radius=std::clamp(int(std::ceil((1-s.fidelity)*4+s.blur)),0,16);
  if(!radius) return d;
  // Separable bilateral filter: O(radius * pixels), preserves thin depth edges.
  Depth tmp(d.width,d.height),out(d.width,d.height);
  const float sigma=std::max(0.6f,radius*0.5f);
  const float range=0.5f*(1-s.edge_preservation)+0.005f;
  auto pass=[&](const Depth& src,Depth& dst,bool vertical) {
    for(int y=0;y<src.height;++y) for(int x=0;x<src.width;++x) {
      float sum=0,weight=0,center=src.at(x,y);
      for(int k=-radius;k<=radius;++k) {
        float v=src.at(std::clamp(x+(vertical?0:k),0,src.width-1),
                       std::clamp(y+(vertical?k:0),0,src.height-1));
        float diff=v-center;
        float w=std::exp(-float(k*k)/(2*sigma*sigma)-diff*diff/(2*range*range));
        sum+=w*v;weight+=w;
      }
      dst.at(x,y)=sum/weight;
    }
  };
  pass(d,tmp,false);pass(tmp,out,true);return out;
}
}
