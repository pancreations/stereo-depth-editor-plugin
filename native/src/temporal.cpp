#include "s3d/pipeline.hpp"

namespace s3d {
Depth stabilize(const Depth& now,const Depth& prev,const Image& guide,
                const Image& old,const TemporalSettings& s,bool cut) {
  now.validate();
  if(!s.enabled || cut) return now;
  prev.validate();guide.validate();old.validate();
  if(now.width!=prev.width || now.height!=prev.height || guide.width!=now.width ||
     guide.height!=now.height || old.width!=now.width || old.height!=now.height)
    throw std::invalid_argument("Temporal input dimensions differ");
  for(float v:{s.strength,s.motion_sensitivity,s.edge_preservation}) {finite(v);if(v<0||v>1) throw std::invalid_argument("Invalid temporal setting");}
  auto error=[](Pixel a,Pixel b) {return (std::abs(a.r-b.r)+std::abs(a.g-b.g)+std::abs(a.b-b.b))/3;};
  double scene=0;
  for(std::size_t i=0;i<guide.pixels.size();++i) scene+=std::min(1.0f,error(guide.pixels[i],old.pixels[i]));
  if(scene/guide.pixels.size()>0.25) return now;
  Depth out=now;
  // Conservative fallback: reject moving/color-changed pixels instead of ghosting.
  // The offline worker supplies optical-flow stabilization for higher quality.
  for(int y=0;y<now.height;++y) for(int x=0;x<now.width;++x) {
    float e=error(guide.at(x,y),old.at(x,y));
    float gradient=std::max(std::abs(now.at(x,y)-now.at(std::max(0,x-1),y)),
                            std::abs(now.at(x,y)-now.at(x,std::max(0,y-1))));
    float motion=std::exp(-e*(10+90*s.motion_sensitivity));
    float agreement=std::exp(-std::abs(now.at(x,y)-prev.at(x,y))*20);
    float edges=std::exp(-gradient*40*s.edge_preservation);
    float w=std::min(0.85f,s.strength)*motion*agreement*edges;
    out.at(x,y)=now.at(x,y)*(1-w)+prev.at(x,y)*w;
  }
  return out;
}
}
