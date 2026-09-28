#include "runtime_renderer.hpp"
#include <chrono>
#include <iostream>
int main() {
  using namespace s3d;
  try {
    Image image(320,180);Depth depth(320,180);
    for(int y=0;y<180;++y)for(int x=0;x<320;++x){image.at(x,y)={x/320.f,-.2f,2.4f,1};depth.at(x,y)=x<160?.2f:.8f;}
    Settings s;s.output=OutputMode::FullSBS;
    for(auto preview:{Preview::Final,Preview::DepthMap,Preview::Left,Preview::Right,Preview::Difference,Preview::Parallax,Preview::Occlusion}) {
      s.preview=preview;auto cpu=render(image,depth,s).output;auto gpu=gpu::render_cuda(image,depth,s);
      if(cpu.width!=gpu.width||cpu.height!=gpu.height)throw std::runtime_error("GPU output dimensions");
      float max_error=0;
      for(size_t i=0;i<cpu.pixels.size();++i){auto a=cpu.pixels[i],b=gpu.pixels[i];for(float v:{b.r,b.g,b.b,b.a})finite(v);max_error=std::max({max_error,std::abs(a.r-b.r),std::abs(a.g-b.g),std::abs(a.b-b.b),std::abs(a.a-b.a)});}
      if(max_error>1e-3)throw std::runtime_error("GPU/CPU mismatch preview "+std::to_string(int(preview))+": "+std::to_string(max_error));
    }
    s.preview=Preview::DepthMap;s.depth.invert=true;auto inverted=gpu::render_cuda(image,depth,s);
    if(std::abs(inverted.at(0,0).r-.8f)>1e-5)throw std::runtime_error("GPU inversion failed");
    s={};s.repair.mode=Reconstruction::High;
    auto high_cpu=render(image,depth,s).output;auto high_gpu=gpu::render_cuda(image,depth,s);
    float high_error=0;
    for(size_t i=0;i<high_cpu.pixels.size();++i){auto a=high_cpu.pixels[i],b=high_gpu.pixels[i];high_error=std::max({high_error,std::abs(a.r-b.r),std::abs(a.g-b.g),std::abs(a.b-b.b)});}
    if(high_error>1e-3)throw std::runtime_error("High repair CPU/GPU mismatch: "+std::to_string(high_error));
    s={};Image hd(1920,1080,Pixel{.8f,.2f,2,1});Depth hd_depth(518,292,.8f);
    gpu::render_cuda(hd,hd_depth,s);
    double elapsed=0;
    for(int i=0;i<12;++i){auto start=std::chrono::steady_clock::now();auto out=gpu::render_cuda(hd,hd_depth,s);elapsed+=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();}
    std::cout<<"CUDA preview parity + inversion passed; 1080p cached-depth stereo incl CPU/GPU transfers: "<<elapsed/12*1000<<" ms/frame ("<<12/elapsed<<" fps), excludes Premiere and inference\n";
    s.repair.mode=Reconstruction::High;
    auto cpu_start=std::chrono::steady_clock::now();auto cpu_high=render(hd,hd_depth,s).output;
    auto cpu_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-cpu_start).count();
    gpu::render_cuda(hd,hd_depth,s);elapsed=0;
    for(int i=0;i<12;++i){auto start=std::chrono::steady_clock::now();auto out=gpu::render_cuda(hd,hd_depth,s);elapsed+=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();}
    std::cout<<"High repair parity passed (max error "<<high_error<<"); 1080p High before CPU "<<cpu_ms<<" ms/frame, after GPU "<<elapsed/12*1000<<" ms/frame\n";
    return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
