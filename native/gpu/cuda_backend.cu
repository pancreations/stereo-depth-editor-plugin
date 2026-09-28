// Experimental CUDA processing building block; NOT yet registered with PrGPUFilter.
#include "s3d/gpu.hpp"
#include <cuda_runtime.h>
#include <cmath>
#include <initializer_list>

namespace s3d::gpu {
template<class T> __device__ T& pixel(Surface p,int x,int y) {
  return reinterpret_cast<T*>(reinterpret_cast<char*>(p.data)+std::size_t(y)*p.pitch)[x];
}
__device__ float clampf(float x,float lo,float hi) {return fminf(hi,fmaxf(lo,x));}
__device__ unsigned key(float v) {unsigned u=__float_as_uint(v);return (u&0x80000000u)?~u:(u^0x80000000u);}
__device__ float4 add(float4 a,float4 b) {return make_float4(a.x+b.x,a.y+b.y,a.z+b.z,a.w+b.w);}
__device__ float4 mul(float4 a,float b) {return make_float4(a.x*b,a.y*b,a.z*b,a.w*b);}
__global__ void remap_kernel(Surface raw,Surface out,DepthRemap s) {
  int x=blockIdx.x*blockDim.x+threadIdx.x,y=blockIdx.y*blockDim.y+threadIdx.y;
  if(x>=raw.width || y>=raw.height) return;
  float v=pixel<float>(raw,x,y);if(!isfinite(v)) v=s.far_point;
  if(s.normalize) v=(v-s.low)/(s.high-s.low);
  if(!s.white_near) v=1-v;
  v=(v-s.far_point)/(s.near_point-s.far_point);
  if(s.invert) v=1-v;
  v=(v-.5f)*s.scale+.5f+s.offset;v=(v-.5f)*s.contrast+.5f;
  v=copysignf(powf(fabsf(v),1/s.gamma),v);
  pixel<float>(out,x,y)=s.clamp?clampf(v,0,1):v;
}
__global__ void bilateral_kernel(Surface src,Surface dst,int radius,float edge,bool vertical) {
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
__global__ void z_kernel(Surface depth,Surface keys,Stereo s,bool left) {
  int x=blockIdx.x*blockDim.x+threadIdx.x,y=blockIdx.y*blockDim.y+threadIdx.y;
  if(x>=depth.width || y>=depth.height) return;
  float z=pixel<float>(depth,x,y),dest=target(x,z,depth.width,s,left);int base=int(floorf(dest));
  for(int k=0;k<2;++k) {int dx=base+k;float w=k?dest-base:1-(dest-base);
    if(dx>=0 && dx<depth.width && w>=1e-6f) atomicMax(&pixel<unsigned>(keys,dx,y),key(z));}
}
__global__ void splat_kernel(Surface rgba,Surface depth,Surface keys,Surface accum,Surface weights,Stereo s,bool left) {
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
__global__ void resolve_kernel(Surface accum,Surface weights,Surface eye,Surface holes) {
  int x=blockIdx.x*blockDim.x+threadIdx.x,y=blockIdx.y*blockDim.y+threadIdx.y;
  if(x>=eye.width || y>=eye.height) return;
  float w=pixel<float>(weights,x,y);pixel<unsigned char>(holes,x,y)=w>0?0:1;
  pixel<float4>(eye,x,y)=w>0?mul(pixel<float4>(accum,x,y),1/w):make_float4(0,0,0,0);
}
__global__ void repair_kernel(Surface eye,Surface original,Surface holes,Surface out,int radius) {
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
__global__ void pack_kernel(Surface left,Surface right,Surface out,bool reversed) {
  int x=blockIdx.x*blockDim.x+threadIdx.x,y=blockIdx.y*blockDim.y+threadIdx.y;
  if(x>=out.width || y>=out.height) return;
  int split=out.width/2;bool second=x>=split;Surface eye=(second!=reversed)?right:left;
  int width=second?out.width-split:split;int xx=second?x-split:x;
  float sx=clampf((xx+.5f)*eye.width/width-.5f,0,float(eye.width-1));int x0=int(sx),x1=min(x0+1,eye.width-1);
  pixel<float4>(out,x,y)=add(mul(pixel<float4>(eye,x0,y),1-(sx-x0)),mul(pixel<float4>(eye,x1,y),sx-x0));
}
static dim3 grid(Surface s) {return dim3((s.width+15)/16,(s.height+15)/16);}
static bool valid(Surface s,std::size_t element) {return s.data && s.width>0 && s.height>0 && s.pitch>=std::size_t(s.width)*element;}
static bool same(Surface a,Surface b) {return a.width==b.width && a.height==b.height;}
int cuda_remap(Surface raw,Surface depth,const DepthRemap& s,void* stream) {
  if(!valid(raw,4)||!valid(depth,4)||!same(raw,depth)||s.gamma<=0||fabsf(s.near_point-s.far_point)<1e-6f||
     (s.normalize&&s.high<=s.low)) return cudaErrorInvalidValue;
  remap_kernel<<<grid(raw),dim3(16,16),0,static_cast<cudaStream_t>(stream)>>>(raw,depth,s);return cudaGetLastError();
}
int cuda_bilateral(Surface src,Surface dst,int radius,float edge,bool vertical,void* stream) {
  if(!valid(src,4)||!valid(dst,4)||!same(src,dst)||src.data==dst.data||radius<0||radius>16||edge<0||edge>1) return cudaErrorInvalidValue;
  bilateral_kernel<<<grid(src),dim3(16,16),0,static_cast<cudaStream_t>(stream)>>>(src,dst,radius,edge,vertical);return cudaGetLastError();
}
int cuda_warp(Surface rgba,Surface depth,Surface keys,Surface accum,Surface weights,Surface eye,Surface holes,const Stereo& s,bool left,void* stream) {
  if(!valid(rgba,16)||!valid(depth,4)||!valid(keys,4)||!valid(accum,16)||!valid(weights,4)||!valid(eye,16)||!valid(holes,1)||
     !same(rgba,depth)||!same(rgba,keys)||!same(rgba,accum)||!same(rgba,weights)||!same(rgba,eye)||!same(rgba,holes)) return cudaErrorInvalidValue;
  auto cuda_stream=static_cast<cudaStream_t>(stream);
  for(Surface surface:{keys,accum,weights}) {
    auto err=cudaMemset2DAsync(surface.data,surface.pitch,0,surface.width*(surface.data==accum.data?16:4),surface.height,cuda_stream);
    if(err!=cudaSuccess) return err;
  }
  z_kernel<<<grid(depth),dim3(16,16),0,cuda_stream>>>(depth,keys,s,left);
  auto err=cudaGetLastError();if(err!=cudaSuccess) return err;
  splat_kernel<<<grid(depth),dim3(16,16),0,cuda_stream>>>(rgba,depth,keys,accum,weights,s,left);
  err=cudaGetLastError();if(err!=cudaSuccess) return err;
  resolve_kernel<<<grid(depth),dim3(16,16),0,cuda_stream>>>(accum,weights,eye,holes);return cudaGetLastError();
}
int cuda_repair_fast(Surface eye,Surface original,Surface holes,Surface result,int radius,void* stream) {
  if(!valid(eye,16)||!valid(original,16)||!valid(holes,1)||!valid(result,16)||!same(eye,original)||!same(eye,holes)||!same(eye,result)||
     eye.data==result.data||radius<1||radius>512) return cudaErrorInvalidValue;
  repair_kernel<<<grid(eye),dim3(16,16),0,static_cast<cudaStream_t>(stream)>>>(eye,original,holes,result,radius);return cudaGetLastError();
}
int cuda_pack(Surface left,Surface right,Surface output,bool reversed,void* stream) {
  if(!valid(left,16)||!valid(right,16)||!valid(output,16)||!same(left,right)||output.width<2||output.height!=left.height) return cudaErrorInvalidValue;
  pack_kernel<<<grid(output),dim3(16,16),0,static_cast<cudaStream_t>(stream)>>>(left,right,output,reversed);return cudaGetLastError();
}
}
