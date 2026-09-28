struct Surface {void* data;int width,height;unsigned long long pitch;};
struct DepthRemap {float low,high,near_point,far_point,offset,scale,contrast,gamma;int normalize,invert,white_near,clamp;};
struct Stereo {float strength,convergence,maximum_percent,separation_percent;int reverse;};
template<class T> __device__ T& pixel(Surface p,int x,int y) {
  return reinterpret_cast<T*>(reinterpret_cast<char*>(p.data)+static_cast<unsigned long long>(y)*p.pitch)[x];
}
__device__ float clampf(float x,float lo,float hi) {return fminf(hi,fmaxf(lo,x));}
__device__ unsigned key(float v) {unsigned u=__float_as_uint(v);return (u&0x80000000u)?~u:(u^0x80000000u);}
__device__ float4 add(float4 a,float4 b) {return make_float4(a.x+b.x,a.y+b.y,a.z+b.z,a.w+b.w);}
__device__ float4 mul(float4 a,float b) {return make_float4(a.x*b,a.y*b,a.z*b,a.w*b);}
extern "C" __global__ void remap_kernel(Surface raw,Surface out,DepthRemap s) {
  int x=blockIdx.x*blockDim.x+threadIdx.x,y=blockIdx.y*blockDim.y+threadIdx.y;
  if(x>=out.width || y>=out.height) return;
  float sx=clampf((x+.5f)*raw.width/out.width-.5f,0,float(raw.width-1));
  float sy=clampf((y+.5f)*raw.height/out.height-.5f,0,float(raw.height-1));
  int x0=int(sx),y0=int(sy),x1=min(x0+1,raw.width-1),y1=min(y0+1,raw.height-1);
  float a=pixel<float>(raw,x0,y0)*(1-(sx-x0))+pixel<float>(raw,x1,y0)*(sx-x0);
  float b=pixel<float>(raw,x0,y1)*(1-(sx-x0))+pixel<float>(raw,x1,y1)*(sx-x0);
  float v=a*(1-(sy-y0))+b*(sy-y0);if(!isfinite(v)) v=s.far_point;
  if(s.normalize) v=(v-s.low)/(s.high-s.low);
  if(!s.white_near) v=1-v;
  v=(v-s.far_point)/(s.near_point-s.far_point);
  if(s.invert) v=1-v;
  v=(v-.5f)*s.scale+.5f+s.offset;v=(v-.5f)*s.contrast+.5f;
  v=copysignf(powf(fabsf(v),1/s.gamma),v);
  pixel<float>(out,x,y)=s.clamp?clampf(v,0,1):v;
}
extern "C" __global__ void bilateral_kernel(Surface src,Surface dst,int radius,float edge,bool vertical) {
  int x=blockIdx.x*blockDim.x+threadIdx.x,y=blockIdx.y*blockDim.y+threadIdx.y;
  if(x>=src.width || y>=src.height) return;
  float center=pixel<float>(src,x,y),sum=0,total=0;
  float sigma=fmaxf(.6f,radius*.5f),range=.5f*(1-edge)+.005f;
  for(int k=-radius;k<=radius;++k) {
    int xx=max(0,min(src.width-1,x+(vertical?0:k))),yy=max(0,min(src.height-1,y+(vertical?k:0)));
    float v=pixel<float>(src,xx,yy),d=v-center;
    float w=expf(-float(k*k)/(2*sigma*sigma)-d*d/(2*range*range));sum+=v*w;total+=w;
  }
  pixel<float>(dst,x,y)=sum/total;
}
__device__ float target(int x,float z,int width,Stereo s,bool left) {
  float limit=s.maximum_percent*.01f*width;
  float d=clampf((z-s.convergence)*s.strength*s.separation_percent*.01f*width,-limit,limit);
  return x+d*(s.reverse?-1:1)*(left?.5f:-.5f);
}
extern "C" __global__ void z_kernel(Surface depth,Surface keys,Stereo s,bool left) {
  int x=blockIdx.x*blockDim.x+threadIdx.x,y=blockIdx.y*blockDim.y+threadIdx.y;
  if(x>=depth.width || y>=depth.height) return;
  float z=pixel<float>(depth,x,y),dest=target(x,z,depth.width,s,left);int base=int(floorf(dest));
  for(int k=0;k<2;++k) {int dx=base+k;float w=k?dest-base:1-(dest-base);
    if(dx>=0 && dx<depth.width && w>=1e-6f) atomicMax(&pixel<unsigned>(keys,dx,y),key(z));}
}
extern "C" __global__ void splat_kernel(Surface rgba,Surface depth,Surface keys,Surface accum,Surface weights,Stereo s,bool left) {
  int x=blockIdx.x*blockDim.x+threadIdx.x,y=blockIdx.y*blockDim.y+threadIdx.y;
  if(x>=depth.width || y>=depth.height) return;
  float z=pixel<float>(depth,x,y),dest=target(x,z,depth.width,s,left);int base=int(floorf(dest));
  for(int k=0;k<2;++k) {int dx=base+k;float w=k?dest-base:1-(dest-base);
    if(dx<0 || dx>=depth.width || w<1e-6f || pixel<unsigned>(keys,dx,y)!=key(z)) continue;
    float4 c=mul(pixel<float4>(rgba,x,y),w);float4& v=pixel<float4>(accum,dx,y);
    atomicAdd(&v.x,c.x);atomicAdd(&v.y,c.y);atomicAdd(&v.z,c.z);atomicAdd(&v.w,c.w);
    atomicAdd(&pixel<float>(weights,dx,y),w);
  }
}
extern "C" __global__ void resolve_kernel(Surface accum,Surface weights,Surface eye,Surface holes) {
  int x=blockIdx.x*blockDim.x+threadIdx.x,y=blockIdx.y*blockDim.y+threadIdx.y;
  if(x>=eye.width || y>=eye.height) return;
  float w=pixel<float>(weights,x,y);pixel<unsigned char>(holes,x,y)=w>0?0:1;
  pixel<float4>(eye,x,y)=w>0?mul(pixel<float4>(accum,x,y),1/w):make_float4(0,0,0,0);
}
extern "C" __global__ void repair_kernel(Surface eye,Surface original,Surface holes,Surface out,int radius) {
  int x=blockIdx.x*blockDim.x+threadIdx.x,y=blockIdx.y*blockDim.y+threadIdx.y;
  if(x>=eye.width || y>=eye.height) return;
  if(!pixel<unsigned char>(holes,x,y)) {pixel<float4>(out,x,y)=pixel<float4>(eye,x,y);return;}
  float4 result=pixel<float4>(original,x,y);
  for(int d=1;d<=radius;++d) {
    if(x-d>=0 && !pixel<unsigned char>(holes,x-d,y)) {result=pixel<float4>(eye,x-d,y);break;}
    if(x+d<eye.width && !pixel<unsigned char>(holes,x+d,y)) {result=pixel<float4>(eye,x+d,y);break;}
  }
  pixel<float4>(out,x,y)=result;
}
__device__ float unkey(unsigned k) {return __uint_as_float((k&0x80000000u)?(k^0x80000000u):~k);}
extern "C" __global__ void high_seed_kernel(Surface eye,Surface original,Surface holes,Surface keys,Surface out,int radius,float softness,float artifact) {
  int x=blockIdx.x*blockDim.x+threadIdx.x,y=blockIdx.y*blockDim.y+threadIdx.y;
  if(x>=eye.width||y>=eye.height)return;
  if(!pixel<unsigned char>(holes,x,y)){pixel<float4>(out,x,y)=pixel<float4>(eye,x,y);return;}
  int l=x-1,r=x+1;
  while(l>=0&&pixel<unsigned char>(holes,l,y))--l;
  while(r<eye.width&&pixel<unsigned char>(holes,r,y))++r;
  bool hasl=l>=0,hasr=r<eye.width;
  float4 value=pixel<float4>(original,x,y);
  if(hasl||hasr) {
    int pick=!hasl?r:!hasr?l:(x-l<=r-x?l:r);
    if(hasl&&hasr) {
      float dz=unkey(pixel<unsigned>(keys,l,y))-unkey(pixel<unsigned>(keys,r,y));
      if(fabsf(dz)>.01f*(1-artifact)+.001f)pick=dz<0?l:r;
      else if(r-l-1<=radius) {
        float t=float(x-l)/float(r-l);
        pixel<float4>(out,x,y)=add(mul(pixel<float4>(eye,l,y),1-t),mul(pixel<float4>(eye,r,y),t));return;
      }
    }
    value=pixel<float4>(eye,pick,y);
    if(abs(x-pick)<=radius) {
      int neighbor=max(0,min(eye.width-1,pick+(pick==l?-1:1)));
      if(!pixel<unsigned char>(holes,neighbor,y))value=add(mul(value,1-softness*.5f),mul(pixel<float4>(eye,neighbor,y),softness*.5f));
    }
  }
  pixel<float4>(out,x,y)=value;
}
__device__ float color_difference(float4 a,float4 b) {
  float r=a.x-b.x,g=a.y-b.y,bl=a.z-b.z,al=a.w-b.w;return r*r+g*g+bl*bl+al*al;
}
extern "C" __global__ void content_fill_kernel(Surface guide,Surface holes,Surface out,int radius,float softness) {
  int x=blockIdx.x*blockDim.x+threadIdx.x,y=blockIdx.y*blockDim.y+threadIdx.y;
  if(x>=guide.width||y>=guide.height)return;
  float4 seed=pixel<float4>(guide,x,y),choice=seed;
  if(!pixel<unsigned char>(holes,x,y)){pixel<float4>(out,x,y)=seed;return;}
  radius=min(radius,64);int step=max(1,radius/6);float best=3.402823466e+38F;
  for(int oy=-radius/2;oy<=radius/2;oy+=step)for(int ox=-radius;ox<=radius;ox+=step) {
    int cx=x+ox,cy=y+oy;
    if(cx<1||cy<1||cx>=guide.width-1||cy>=guide.height-1||pixel<unsigned char>(holes,cx,cy))continue;
    float score=0,weight=0;bool complete=true;
    for(int py=-1;py<=1;++py)for(int px=-1;px<=1;++px) {
      if(pixel<unsigned char>(holes,cx+px,cy+py)){complete=false;continue;}
      int tx=max(0,min(guide.width-1,x+px)),ty=max(0,min(guide.height-1,y+py));
      float w=pixel<unsigned char>(holes,tx,ty)?.2f:1.f;
      score+=w*color_difference(pixel<float4>(guide,tx,ty),pixel<float4>(guide,cx+px,cy+py));weight+=w;
    }
    if(!complete||weight<=0)continue;
    score=score/weight+.00001f*float(ox*ox+oy*oy)/float(radius*radius);
    if(score<best){best=score;choice=pixel<float4>(guide,cx,cy);}
  }
  float t=1-softness*.5f;pixel<float4>(out,x,y)=add(mul(seed,1-t),mul(choice,t));
}
extern "C" __global__ void pack_kernel(Surface left,Surface right,Surface out,bool reversed) {
  int x=blockIdx.x*blockDim.x+threadIdx.x,y=blockIdx.y*blockDim.y+threadIdx.y;
  if(x>=out.width || y>=out.height) return;
  int split=out.width/2;bool second=x>=split;Surface eye=(second!=reversed)?right:left;
  int width=second?out.width-split:split;int xx=second?x-split:x;
  float sx=clampf((xx+.5f)*eye.width/width-.5f,0,float(eye.width-1));int x0=int(sx),x1=min(x0+1,eye.width-1);
  pixel<float4>(out,x,y)=add(mul(pixel<float4>(eye,x0,y),1-(sx-x0)),mul(pixel<float4>(eye,x1,y),sx-x0));
}

extern "C" __global__ void preview_kernel(Surface left,Surface right,Surface lh,Surface rh,Surface depth,Surface output,int mode,Stereo stereo) {
  int x=blockIdx.x*blockDim.x+threadIdx.x,y=blockIdx.y*blockDim.y+threadIdx.y;
  if(x>=output.width||y>=output.height)return;
  float4 color;
  if(mode==2){float z=pixel<float>(depth,x,y);color=make_float4(z,z,z,1);}
  else if(mode==3)color=pixel<float4>(left,x,y);
  else if(mode==4)color=pixel<float4>(right,x,y);
  else if(mode==5){float4 a=pixel<float4>(left,x,y),b=pixel<float4>(right,x,y);color=make_float4(fabsf(a.x-b.x),fabsf(a.y-b.y),fabsf(a.z-b.z),1);}
  else if(mode==6){float limit=fmaxf(1e-6f,stereo.maximum_percent*.01f*depth.width);float d=(target(x,pixel<float>(depth,x,y),depth.width,stereo,true)-x)*2/limit;color=make_float4(fmaxf(d,0),1-fabsf(d),fmaxf(-d,0),1);}
  else color=make_float4(pixel<unsigned char>(lh,x,y),0,pixel<unsigned char>(rh,x,y),1);
  pixel<float4>(output,x,y)=color;
}

