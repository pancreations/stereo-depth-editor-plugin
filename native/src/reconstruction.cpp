#include "s3d/pipeline.hpp"

namespace s3d {
void reconstruct(Eye& eye,const Image& original,const RepairSettings& s,InpaintingProvider* provider) {
  if(s.mode==Reconstruction::AI) {
    if(!provider) throw std::runtime_error("AI inpainting provider is not installed");
    auto filled=provider->fill(eye.image,eye.holes,original,s);filled.validate();
    if(filled.width!=original.width || filled.height!=original.height) throw std::runtime_error("Inpainting provider returned wrong dimensions");
    for(std::size_t i=0;i<filled.pixels.size();++i) {
      if(!eye.holes.pixels[i]) filled.pixels[i]=eye.image.pixels[i];
      else for(float v:{filled.pixels[i].r,filled.pixels[i].g,filled.pixels[i].b,filled.pixels[i].a}) finite(v);
    }
    eye.image=std::move(filled);return;
  }
  if(s.radius<1 || s.radius>512) throw std::invalid_argument("Reconstruction radius must be 1..512");
  for(float v:{s.softness,s.artifact_reduction}) {finite(v);if(v<0||v>1) throw std::invalid_argument("Invalid repair setting");}
  // Keep the original visibility mask for debugging and future model input.
  const Image immutable=eye.image;
  for(int y=0;y<eye.image.height;++y) {
    int x=0;
    while(x<eye.image.width) {
      if(!eye.holes.at(x,y)) {++x;continue;}
      int begin=x;
      while(x<eye.image.width && eye.holes.at(x,y)) ++x;
      int end=x,l=begin-1,r=end;
      for(int px=begin;px<end;++px) {
        // Off disables repair but remains opaque using the original camera sample.
        Pixel value=original.at(px,y);
        if(s.mode!=Reconstruction::Off) {
          bool hasl=l>=0,hasr=r<eye.image.width;
          if(hasl || hasr) {
            int pick=!hasl?r:!hasr?l:(px-l<=r-px?l:r);
            if(s.mode==Reconstruction::High && hasl && hasr) {
              // Prefer the farther surface; foreground stretching creates halos.
              float dz=eye.z.at(l,y)-eye.z.at(r,y);
              if(std::abs(dz)>0.01f*(1-s.artifact_reduction)+0.001f) pick=dz<0?l:r;
              else if(end-begin<=s.radius) {
                value=mix(immutable.at(l,y),immutable.at(r,y),float(px-l)/float(r-l));
                eye.image.at(px,y)=value;continue;
              }
            }
            value=immutable.at(pick,y);
            // Radius limits textured extrapolation; wider gaps use stable edge extension.
            if(s.mode==Reconstruction::High && std::abs(px-pick)<=s.radius) {
              int neighbor=std::clamp(pick+(pick==l?-1:1),0,eye.image.width-1);
              if(!eye.holes.at(neighbor,y)) value=mix(value,immutable.at(neighbor,y),s.softness*0.5f);
            }
          }
        }
        eye.image.at(px,y)=value;
      }
    }
  }
  if(s.mode==Reconstruction::High) content_fill(eye,s);
}
}
