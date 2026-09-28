#define NOMINMAX
#include <windows.h>
#include "runtime_renderer.hpp"
#include "runtime_config.hpp"
#include "s3d/gpu.hpp"
#include "buffer_pool.hpp"
#include <fstream>
#include <mutex>
#include <string>
#include <array>
#include <map>

namespace s3d::gpu {
namespace {
using DevicePointer=unsigned long long;
template<class... Args> int api(HMODULE library,const char* name,Args... args) {
  auto fn=reinterpret_cast<int(*)(Args...)>(GetProcAddress(library,name));
  if(!fn)throw std::runtime_error(std::string("Missing CUDA function: ")+name);
  int result=fn(args...);
  if(result)throw std::runtime_error(std::string(name)+" failed ("+std::to_string(result)+")");
  return result;
}
struct Runtime {
  HMODULE driver=nullptr,compiler=nullptr,builtins=nullptr;void* context=nullptr;void* module=nullptr;void* program=nullptr;
  std::map<std::string,void*> functions;BufferPool<13> pool;
  std::array<std::uint64_t,13>& buffers=pool.pointers;
  int width=0,height=0;std::mutex mutex;
  Runtime() {try {
    driver=LoadLibraryW(L"nvcuda.dll");
    std::vector<std::filesystem::path> search;
    if(const auto* dir=_wgetenv(L"S3D_NVRTC_DIR"))search.emplace_back(dir);
    if(const auto* python=_wgetenv(L"S3D_PYTHON"))
      search.push_back(std::filesystem::path(python).parent_path().parent_path()/"Lib/site-packages/torch/lib");
    search.push_back(s3d::project_root()/".venv310/Lib/site-packages/torch/lib");
    if(const auto* cuda=_wgetenv(L"CUDA_PATH"))search.push_back(std::filesystem::path(cuda)/"bin");
    for(const auto& dir:search) {
      if(!std::filesystem::is_directory(dir))continue;
      std::filesystem::path dll,builtin;
      for(const auto& file:std::filesystem::directory_iterator(dir)) {
        const auto name=file.path().filename().string();
        if(file.path().extension()!=".dll")continue;
        if(name.rfind("nvrtc64_",0)==0)dll=file.path();
        if(name.rfind("nvrtc-builtins64_",0)==0)builtin=file.path();
      }
      if(dll.empty())continue;
      if(!builtin.empty())builtins=LoadLibraryExW(builtin.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
      compiler=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
      if(compiler)break;
      if(builtins){FreeLibrary(builtins);builtins=nullptr;}
    }
    if(!driver||!compiler)throw std::runtime_error("NVIDIA CUDA/NVRTC unavailable. Install CUDA PyTorch for GPU stereo.");
    api(driver,"cuInit",0u);int device=0;api(driver,"cuDeviceGet",&device,0);
    api(driver,"cuDevicePrimaryCtxRetain",&context,device);
    api(driver,"cuCtxPushCurrent_v2",context);
    try {
      std::ifstream f(s3d::project_root()/"native/gpu/runtime_kernels.cu");
      std::string source((std::istreambuf_iterator<char>(f)),{});if(source.empty())throw std::runtime_error("CUDA kernel source missing");
      api(compiler,"nvrtcCreateProgram",&program,source.c_str(),"stereodepth.cu",0,static_cast<const char**>(nullptr),static_cast<const char**>(nullptr));
      int major=0,minor=0,count=0;
      api(driver,"cuDeviceGetAttribute",&major,75,device);
      api(driver,"cuDeviceGetAttribute",&minor,76,device);
      api(compiler,"nvrtcGetNumSupportedArchs",&count);
      std::vector<int> archs(count);api(compiler,"nvrtcGetSupportedArchs",archs.data());
      int target=0;for(int arch:archs)if(arch<=major*10+minor)target=std::max(target,arch);
      if(!target)throw std::runtime_error("NVRTC does not support this GPU; install CUDA 12.8 or newer");
      const auto architecture="--gpu-architecture=compute_"+std::to_string(target);
      const char* options[]={architecture.c_str(),"--std=c++14"};
      try {api(compiler,"nvrtcCompileProgram",program,2,options);}
      catch(...) {
        size_t bytes=0;api(compiler,"nvrtcGetProgramLogSize",program,&bytes);std::string log(bytes,0);api(compiler,"nvrtcGetProgramLog",program,log.data());
        api(compiler,"nvrtcDestroyProgram",&program);throw std::runtime_error("CUDA compile: "+log);
      }
      size_t bytes=0;api(compiler,"nvrtcGetPTXSize",program,&bytes);std::string ptx(bytes,0);api(compiler,"nvrtcGetPTX",program,ptx.data());api(compiler,"nvrtcDestroyProgram",&program);
      api(driver,"cuModuleLoadData",&module,ptx.c_str());
      for(const char* name:{"remap_kernel","bilateral_kernel","z_kernel","splat_kernel","resolve_kernel","repair_kernel","high_seed_kernel","content_fill_kernel","pack_kernel","preview_kernel"}) {
        void* function=nullptr;api(driver,"cuModuleGetFunction",&function,module,name);functions[name]=function;
      }
    } catch(...) {void* previous;api(driver,"cuCtxPopCurrent_v2",&previous);throw;}
    void* previous;api(driver,"cuCtxPopCurrent_v2",&previous);
    }catch(...){cleanup();throw;}
  }
  void free_buffer(std::uint64_t pointer) noexcept {
    auto fn=reinterpret_cast<int(*)(DevicePointer)>(GetProcAddress(driver,"cuMemFree_v2"));
    if(fn)fn(pointer);
  }
  void cleanup() noexcept {
    if(program&&compiler){auto destroy=reinterpret_cast<int(*)(void**)>(GetProcAddress(compiler,"nvrtcDestroyProgram"));if(destroy)destroy(&program);}
    try {
      if(context){api(driver,"cuCtxPushCurrent_v2",context);pool.clear([&](auto p){free_buffer(p);});
        if(module)api(driver,"cuModuleUnload",module);void* previous=nullptr;api(driver,"cuCtxPopCurrent_v2",&previous);
        api(driver,"cuDevicePrimaryCtxRelease_v2",0);}
    }catch(...){} // Driver may already be shutting down at process exit.
    if(compiler)FreeLibrary(compiler);if(builtins)FreeLibrary(builtins);if(driver)FreeLibrary(driver);
    compiler=builtins=driver=nullptr;context=module=nullptr;
  }
  ~Runtime(){cleanup();}
  void size(int w,int h,const Settings& settings) {
    // source, depth, filtered depth, z, accumulator, weights, left, right,
    // left holes, right holes, repaired left, repaired right, 2W output.
    std::array<size_t,13> strides{16,4,4,4,16,4,16,16,1,1,16,16,16};
    if(settings.preview==Preview::DepthMap||settings.preview==Preview::Parallax)
      strides={0,4,4,0,0,0,0,0,0,0,0,0,16};
    if(settings.preview==Preview::Final&&settings.output==OutputMode::FullSBS)strides[12]=32;
    for(auto& size:strides)size*=size_t(w)*h;
    width=height=0;
    pool.resize(w,h,strides,[&](size_t bytes){DevicePointer pointer=0;api(driver,"cuMemAlloc_v2",&pointer,bytes);return std::uint64_t(pointer);},
        [&](auto pointer){free_buffer(pointer);});
    width=pool.width;height=pool.height;
  }
  Surface surface(int index,int bytes,int w=0) {return {reinterpret_cast<void*>(buffers[index]),w?w:width,height,size_t(width)*bytes};}
  template<class... Args> void kernel(const char* name,int w,int h,Args... args) {
    void* values[]={static_cast<void*>(&args)...};
    api(driver,"cuLaunchKernel",functions.at(name),unsigned((w+15)/16),unsigned((h+15)/16),1u,16u,16u,1u,0u,static_cast<void*>(nullptr),values,static_cast<void**>(nullptr));
  }
};
Runtime& runtime() {static Runtime value;return value;}
struct CurrentContext {
  Runtime& r;
  CurrentContext(Runtime& value):r(value){api(r.driver,"cuCtxPushCurrent_v2",r.context);}
  ~CurrentContext(){void* previous=nullptr;auto fn=reinterpret_cast<int(*)(void**)>(GetProcAddress(r.driver,"cuCtxPopCurrent_v2"));if(fn)fn(&previous);}
};
}
Image render_cuda(const Image& src,const Depth& raw,const Settings& s) {
  if(s.preview==Preview::Original)return src;
  if(s.repair.mode==Reconstruction::AI)return render(src,raw,s).output;
  auto& r=runtime();std::lock_guard guard(r.mutex);CurrentContext current(r);r.size(src.width,src.height,s);
  Depth reduced;const Depth* depth=&raw;
  if(raw.pixels.size()>src.pixels.size()){reduced=resize_depth(raw,src.width,src.height);depth=&reduced;}
  const auto count=src.pixels.size();
  if(r.buffers[0])api(r.driver,"cuMemcpyHtoD_v2",r.buffers[0],src.pixels.data(),count*16);
  api(r.driver,"cuMemcpyHtoD_v2",r.buffers[1],depth->pixels.data(),depth->pixels.size()*4);
  const auto& d=s.depth;
  if(d.gamma<=0||std::abs(d.near_point-d.far_point)<1e-6f||(d.normalize&&d.normalization_high<=d.normalization_low))throw std::runtime_error("Invalid depth remapping range");
  DepthRemap remap{d.normalization_low,d.normalization_high,d.near_point,d.far_point,d.offset,d.scale,d.contrast,d.gamma,d.normalize,d.invert,d.white_near,d.clamp};
  auto input=r.surface(0,16),processed=r.surface(2,4),scratch=r.surface(1,4);
  Surface raw_surface{reinterpret_cast<void*>(r.buffers[1]),depth->width,depth->height,size_t(depth->width)*4};
  r.kernel("remap_kernel",src.width,src.height,raw_surface,processed,remap);
  int radius=std::clamp(int(std::ceil((1-d.fidelity)*4+d.blur)),0,16);
  if(radius) {
    r.kernel("bilateral_kernel",src.width,src.height,processed,scratch,radius,d.edge_preservation,false);
    r.kernel("bilateral_kernel",src.width,src.height,scratch,processed,radius,d.edge_preservation,true);
  }
  Stereo stereo{s.stereo.strength,s.stereo.convergence,s.stereo.max_parallax_percent,s.stereo.eye_separation_percent,s.stereo.reverse_depth};
  auto left=r.surface(6,16),right=r.surface(7,16),lh=r.surface(8,1),rh=r.surface(9,1),fixed_left=r.surface(10,16),fixed_right=r.surface(11,16);
  if(s.preview!=Preview::DepthMap&&s.preview!=Preview::Parallax) {
    for(int eye=0;eye<2;++eye) {
      auto keys=r.surface(3,4),accum=r.surface(4,16),weights=r.surface(5,4),target=eye?right:left,holes=eye?rh:lh;
      for(int index:{3,4,5})api(r.driver,"cuMemsetD8_v2",r.buffers[index],static_cast<unsigned char>(0),count*(index==4?16:4));
      r.kernel("z_kernel",src.width,src.height,processed,keys,stereo,eye==0);
      r.kernel("splat_kernel",src.width,src.height,input,processed,keys,accum,weights,stereo,eye==0);
      r.kernel("resolve_kernel",src.width,src.height,accum,weights,target,holes);
      auto repaired=eye?fixed_right:fixed_left;
      if(s.repair.mode==Reconstruction::High) {
        r.kernel("high_seed_kernel",src.width,src.height,target,input,holes,keys,repaired,s.repair.radius,s.repair.softness,s.repair.artifact_reduction);
        r.kernel("content_fill_kernel",src.width,src.height,repaired,holes,target,s.repair.radius,s.repair.softness);
      }else r.kernel("repair_kernel",src.width,src.height,target,input,holes,repaired,s.repair.mode==Reconstruction::Off?0:s.repair.radius);
    }
  }
  const bool final=s.preview==Preview::Final;
  if(s.repair.mode==Reconstruction::High){fixed_left=left;fixed_right=right;}
  int out_width=src.width*(final&&s.output==OutputMode::FullSBS?2:1);
  auto output=r.surface(12,16,out_width);output.pitch=size_t(out_width)*16;
  if(final)r.kernel("pack_kernel",out_width,src.height,fixed_left,fixed_right,output,s.right_first);
  else r.kernel("preview_kernel",out_width,src.height,fixed_left,fixed_right,lh,rh,processed,output,int(s.preview),stereo);
  Image result(out_width,src.height);
  api(r.driver,"cuMemcpyDtoH_v2",result.pixels.data(),r.buffers[12],result.pixels.size()*16);
  return result;
}
}
