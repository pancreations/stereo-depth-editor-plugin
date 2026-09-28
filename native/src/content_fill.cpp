#include "s3d/pipeline.hpp"

namespace s3d {
// Bounded exemplar patch search, seeded by background-aware edge extension.
// Search only immutable originally visible patches; never propagate invented data.
void content_fill(Eye& eye,const RepairSettings& settings) {
  const Image guide=eye.image;
  int radius=std::min(settings.radius,64),step=std::max(1,radius/6);
  auto difference=[](Pixel a,Pixel b) {
    float dr=a.r-b.r,dg=a.g-b.g,db=a.b-b.b,da=a.a-b.a;
    return dr*dr+dg*dg+db*db+da*da;
  };
  for(int y=0;y<guide.height;++y) for(int x=0;x<guide.width;++x) {
    if(!eye.holes.at(x,y)) continue;
    float best=std::numeric_limits<float>::infinity();Pixel choice=guide.at(x,y);
    for(int oy=-radius/2;oy<=radius/2;oy+=step) for(int ox=-radius;ox<=radius;ox+=step) {
      int cx=x+ox,cy=y+oy;
      if(cx<1 || cy<1 || cx>=guide.width-1 || cy>=guide.height-1 || eye.holes.at(cx,cy)) continue;
      float score=0,weight=0;bool complete=true;
      for(int py=-1;py<=1;++py) for(int px=-1;px<=1;++px) {
        if(eye.holes.at(cx+px,cy+py)) {complete=false;continue;}
        int tx=std::clamp(x+px,0,guide.width-1),ty=std::clamp(y+py,0,guide.height-1);
        // Match known boundary context more strongly than the extrapolated seed.
        float w=eye.holes.at(tx,ty)?0.2f:1.0f;
        score+=w*difference(guide.at(tx,ty),guide.at(cx+px,cy+py));weight+=w;
      }
      if(!complete || weight<=0) continue;
      score=score/weight+0.00001f*float(ox*ox+oy*oy)/float(radius*radius);
      if(score<best) {best=score;choice=guide.at(cx,cy);}
    }
    // Keep the conservative background seed near difficult boundaries; texture
    // synthesis is a controllable refinement, not generative scene recovery.
    eye.image.at(x,y)=mix(guide.at(x,y),choice,1-settings.softness*0.5f);
  }
}
}
