#include "s3d/pipeline.hpp"

namespace s3d {
template<class T> static Plane<T> resample(const Plane<T>& src,int w,int h) {
  src.validate();Plane<T> out(w,h);
  for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
    float sx=std::clamp((x+0.5f)*src.width/w-0.5f,0.0f,float(src.width-1));
    float sy=std::clamp((y+0.5f)*src.height/h-0.5f,0.0f,float(src.height-1));
    int x0=int(sx),y0=int(sy),x1=std::min(x0+1,src.width-1),y1=std::min(y0+1,src.height-1);
    float fx=sx-x0,fy=sy-y0;
    out.at(x,y)=(src.at(x0,y0)*(1-fx)+src.at(x1,y0)*fx)*(1-fy)+
                (src.at(x0,y1)*(1-fx)+src.at(x1,y1)*fx)*fy;
  }
  return out;
}
Image resize(const Image& src,int w,int h) {return resample(src,w,h);}
Depth resize_depth(const Depth& src,int w,int h) {return resample(src,w,h);}
Image trim_transparent_canvas(Image src) {
  src.validate();
  int left=src.width,top=src.height,right=0,bottom=0;
  for(int y=0;y<src.height;++y)for(int x=0;x<src.width;++x) {
    if(src.at(x,y).a!=0) {
      left=std::min(left,x);top=std::min(top,y);
      right=std::max(right,x+1);bottom=std::max(bottom,y+1);
    }
  }
  if(right<=left || bottom<=top || (left==0 && top==0 && right==src.width && bottom==src.height))
    return src;
  Image cropped(right-left,bottom-top);
  for(int y=0;y<cropped.height;++y)
    std::copy_n(&src.at(left,top+y),cropped.width,&cropped.at(0,y));
  return cropped;
}
namespace {
void fit_region(const Image& src,int sx,int sw,Image& out,int dx,int dw,
                double aspect,CanvasFit fit) {
  // Work in destination pixel coordinates. Half SBS supplies the squeezed
  // aspect, so Fit/Fill still preserves the aspect after stereo playback.
  const double scale=fit==CanvasFit::Fill
    ?std::max(double(dw)/aspect,double(out.height))
    :std::min(double(dw)/aspect,double(out.height));
  const double fw=std::max(1.0,std::round(aspect*scale));
  const double fh=std::max(1.0,std::round(scale));
  const double ox=std::floor((dw-fw)/2),oy=std::floor((out.height-fh)/2);
  for(int y=0;y<out.height;++y) for(int x=0;x<dw;++x) {
    if(x<ox || x>=ox+fw || y<oy || y>=oy+fh)continue;
    const double px=std::clamp((x-ox+0.5)*sw/fw-0.5,0.0,double(sw-1));
    const double py=std::clamp((y-oy+0.5)*src.height/fh-0.5,0.0,double(src.height-1));
    const int x0=int(px),x1=std::min(x0+1,sw-1),y0=int(py),y1=std::min(y0+1,src.height-1);
    const float fx=float(px-x0),fy=float(py-y0);
    out.at(dx+x,y)=mix(mix(src.at(sx+x0,y0),src.at(sx+x1,y0),fx),
                       mix(src.at(sx+x0,y1),src.at(sx+x1,y1),fx),fy);
  }
}
}
Image fit_canvas(const Image& src,int w,int h,CanvasFit fit) {
  src.validate();
  Image out(w,h,Pixel{0,0,0,1});
  fit_region(src,0,src.width,out,0,w,double(src.width)/src.height,fit);
  return out;
}
Image fit_sbs_canvas(const Image& src,int w,int h,OutputMode mode,double aspect,CanvasFit fit) {
  src.validate();
  if(src.width<2 || w<2 || !std::isfinite(aspect) || aspect<=0)
    throw std::invalid_argument("Invalid SBS canvas or source aspect");
  Image out(w,h,Pixel{0,0,0,1});
  const int first=src.width/2,dest_first=w/2;
  if(mode==OutputMode::HalfSBS)aspect/=2;
  fit_region(src,0,first,out,0,dest_first,aspect,fit);
  fit_region(src,first,src.width-first,out,dest_first,w-dest_first,aspect,fit);
  return out;
}
Image pack_sbs(const Image& left,const Image& right,OutputMode mode,bool right_first) {
  left.validate();right.validate();
  if(left.width!=right.width || left.height!=right.height) throw std::invalid_argument("Eye dimensions differ");
  int w=mode==OutputMode::FullSBS?left.width*2:left.width;
  if(w<2) throw std::invalid_argument("SBS requires at least 2 output pixels");
  int a=w/2,b=w-a; // odd widths: right half owns the extra column; no uninitialized seam.
  Image first=resize(right_first?right:left,a,left.height);
  Image second=resize(right_first?left:right,b,left.height);
  Image out(w,left.height);
  for(int y=0;y<out.height;++y) {
    std::copy_n(&first.at(0,y),a,&out.at(0,y));
    std::copy_n(&second.at(0,y),b,&out.at(a,y));
  }
  return out;
}
}
