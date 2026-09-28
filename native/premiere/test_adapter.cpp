// Exercises the actual DLL with minimal SDK callbacks; not an in-Premiere test.
#include "AEConfig.h"
#include "AE_Effect.h"
#include "SPBasic.h"
#include "PrSDKAESupport.h"
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstring>
#include <chrono>
#include "s3d/pipeline.hpp"
#include "depth_bridge.hpp"

namespace {
std::vector<PF_ParamDef> definitions(1);
PF_PixelFormatSuite1 pixels{};
bool cleared=false, added=false;
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
SPErr SPAPI acquire(const char* name,int version,const void** result) {
  if(std::strcmp(name,kPFPixelFormatSuite) || version!=kPFPixelFormatSuiteVersion1) return -1;
  *result=&pixels;return 0;
}
SPErr SPAPI release(const char*,int) {return 0;}
PF_Err clear(PF_ProgPtr) {cleared=true;return 0;}
PF_Err add_format(PF_ProgPtr,PrPixelFormat format) {added=format==PrPixelFormat_BGRA_4444_32f;return 0;}
PF_Err get_format(PF_EffectWorld*,PrPixelFormat* format) {*format=PrPixelFormat_BGRA_4444_32f;return 0;}
PF_Err abort_render(PF_ProgPtr){return 0;}
PF_Err add_param(PF_ProgPtr,PF_ParamIndex,PF_ParamDefPtr definition) {
  definitions.push_back(*definition);return 0;
}
}
int main(int argc,char** argv) {
  HMODULE dll=nullptr;
  try {
    check(argc==2||argc==4||argc==5||argc==6,"Supply plugin path [source.pfm output-directory [external-depth [--sequence]]]");
    dll=LoadLibraryW(std::filesystem::path(argv[1]).c_str());check(dll!=nullptr,"Plugin DLL load failed");
    check(FindResourceW(dll,MAKEINTRESOURCEW(16000),L"PiPL")!=nullptr,"Missing PiPL resource");
    using Entry=PF_Err (*)(PF_Cmd,PF_InData*,PF_OutData*,PF_ParamDef*[],PF_LayerDef*,void*);
    auto entry=reinterpret_cast<Entry>(GetProcAddress(dll,"EffectMain"));check(entry!=nullptr,"Missing EffectMain export");
    pixels.ClearSupportedPixelFormats=clear;pixels.AddSupportedPixelFormat=add_format;pixels.GetPixelFormat=get_format;
    SPBasicSuite basic{};basic.AcquireSuite=acquire;basic.ReleaseSuite=release;
    PF_InData in{};PF_OutData out{};in.pica_basicP=&basic;in.appl_id=0x50724D72;in.inter.add_param=add_param;
    check(entry(PF_Cmd_GLOBAL_SETUP,&in,&out,nullptr,nullptr,nullptr)==0,"Premiere registration failed");
    check(cleared&&added,"Float format registration failed");
    check(entry(PF_Cmd_PARAMS_SETUP,&in,&out,nullptr,nullptr,nullptr)==0,"Parameter registration failed");
    check(out.num_params==definitions.size()&&definitions.size()==46,"Wrong parameter count");
    bool invert=false;
    std::vector<PF_ParamDef*> params;
    for(auto& def:definitions) {
      if(!std::strcmp(def.PF_DEF_NAME,"Invert Depth Map")) invert=def.param_type==PF_Param_CHECKBOX;
      params.push_back(&def);
    }
    check(invert,"Missing Invert Depth Map checkbox");
    float source[8]={2,-.1f,.3f,1,4,5,6,1},destination[8]={};
    auto& world=definitions[0].u.ld;world.width=2;world.height=1;world.rowbytes=sizeof(source);
    world.data=reinterpret_cast<PF_Pixel*>(source);
    PF_LayerDef output=world;output.data=reinterpret_cast<PF_Pixel*>(destination);in.field=PF_Field_FRAME;
    params[2]->u.pd.value=2; // Explicit Original preview never starts inference.
    check(entry(PF_Cmd_RENDER,&in,&out,params.data(),&output,nullptr)==0,"Original preview failed");
    check(std::memcmp(source,destination,sizeof(source))==0,"Original preview changed float color");
    std::vector<float> fitted(4*4*4);
    output.width=4;output.height=4;output.rowbytes=4*16;output.data=reinterpret_cast<PF_Pixel*>(fitted.data());
    for(int fit:{1,2}) {
      params[5]->u.pd.value=fit;
      check(entry(PF_Cmd_RENDER,&in,&out,params.data(),&output,nullptr)==0,"Canvas-sized Original preview failed");
      check(fitted[3]==1,"Original canvas alpha missing");
      check(fit==1?fitted[0]==0:fitted[0]>0,"Original Fit/Fill framing failed");
    }
    params[5]->u.pd.value=1;output=world;output.data=reinterpret_cast<PF_Pixel*>(destination);
    params[2]->u.pd.value=3;params[17]->u.bd.value=TRUE;
    check(entry(PF_Cmd_RENDER,&in,&out,params.data(),&output,nullptr)!=0,"Missing external source silently passed through");
    check(std::strstr(out.return_msg,"Browse Depth Source")!=nullptr,"Missing source did not explain how to select depth");
    if(argc>=4) {
      auto image=s3d::read_pfm_image(argv[2]);auto folder=std::filesystem::path(argv[3]);std::filesystem::create_directories(folder);
      std::vector<float> bgra(image.pixels.size()*4),rendered(bgra.size());
      for(size_t i=0;i<image.pixels.size();++i){auto p=image.pixels[i];bgra[i*4]=p.b;bgra[i*4+1]=p.g;bgra[i*4+2]=p.r;bgra[i*4+3]=p.a;}
      world.width=image.width;world.height=image.height;world.rowbytes=image.width*16;world.data=reinterpret_cast<PF_Pixel*>(bgra.data());
      output=world;output.data=reinterpret_cast<PF_Pixel*>(rendered.data());in.time_scale=30;in.time_step=1;in.inter.abort=abort_render;
      params[17]->u.bd.value=FALSE;
      if(argc>=5) {
        std::string name;params[19]->u.sd.value=s3d::premiere::register_depth_source(std::filesystem::path(argv[4]).wstring(),name);
        params[17]->u.bd.value=TRUE;
      }
      for(int mode:{3,1,3}) {
        params[2]->u.pd.value=mode;
        auto start=std::chrono::steady_clock::now();auto error=entry(PF_Cmd_RENDER,&in,&out,params.data(),&output,nullptr);
        if(error)throw std::runtime_error(out.return_msg);
        std::cout<<"Actual plugin render mode "<<mode<<": "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<" ms\n";
        s3d::Image result(image.width,image.height);
        for(size_t i=0;i<result.pixels.size();++i)result.pixels[i]={rendered[i*4+2],rendered[i*4+1],rendered[i*4],rendered[i*4+3]};
        s3d::write_pfm(result,folder/(mode==3?"depth.pfm":"half-sbs.pfm"));
        check(std::memcmp(bgra.data(),rendered.data(),bgra.size()*sizeof(float))!=0,"Depth/SBS preview still passed through original pixels");
      }
      // Exercise the actual DLL across input/output sizes, not just the packer.
      params[2]->u.pd.value=1;params[8]->u.fs_d.value=0;
      for(int mode:{1,2}) for(int fit:{1,2}) for(int factor:{1,4}) {
        params[3]->u.pd.value=mode;params[5]->u.pd.value=fit;
        output.width=std::max(2,image.width*factor/2+1);output.height=std::max(1,image.height*factor/2);
        output.rowbytes=output.width*16;rendered.resize(size_t(output.width)*output.height*4);
        output.data=reinterpret_cast<PF_Pixel*>(rendered.data());
        auto error=entry(PF_Cmd_RENDER,&in,&out,params.data(),&output,nullptr);
        if(error)throw std::runtime_error(out.return_msg);
        auto packing=mode==2?s3d::OutputMode::FullSBS:s3d::OutputMode::HalfSBS;
        auto expected=s3d::fit_sbs_canvas(s3d::pack_sbs(image,image,packing),output.width,output.height,
          packing,double(image.width)/image.height,fit==2?s3d::CanvasFit::Fill:s3d::CanvasFit::Fit);
        for(size_t i=0;i<expected.pixels.size();++i) {
          auto p=expected.pixels[i];
          check(std::abs(p.r-rendered[i*4+2])<1e-4f && std::abs(p.g-rendered[i*4+1])<1e-4f &&
            std::abs(p.b-rendered[i*4])<1e-4f && std::abs(p.a-rendered[i*4+3])<1e-4f,
            "Actual DLL lost source content or used incorrect SBS canvas fitting");
        }
      }
      std::cout<<"Actual plugin Half/Full SBS Fit/Fill on larger/smaller odd-width canvases passed\n";
      // Premiere Motion can place a smaller opaque clip inside a transparent
      // sequence-sized input. Packing that padding creates a gap between eyes.
      const int padded_width=image.width+image.width/3,padded_height=image.height+image.height/3;
      std::vector<float> padded(size_t(padded_width)*padded_height*4,0);
      const int pad_x=(padded_width-image.width)/2,pad_y=(padded_height-image.height)/2;
      for(int y=0;y<image.height;++y)
        std::copy_n(bgra.data()+size_t(y)*image.width*4,size_t(image.width)*4,
          padded.data()+(size_t(y+pad_y)*padded_width+pad_x)*4);
      const auto original_world=world;
      world.width=padded_width;world.height=padded_height;world.rowbytes=padded_width*16;
      world.data=reinterpret_cast<PF_Pixel*>(padded.data());
      for(int mode:{1,2}) for(int fit:{1,2}) {
        params[3]->u.pd.value=mode;params[5]->u.pd.value=fit;
        rendered.resize(padded.size());output=world;output.data=reinterpret_cast<PF_Pixel*>(rendered.data());
        auto error=entry(PF_Cmd_RENDER,&in,&out,params.data(),&output,nullptr);
        if(error)throw std::runtime_error(out.return_msg);
        auto packing=mode==2?s3d::OutputMode::FullSBS:s3d::OutputMode::HalfSBS;
        auto expected=s3d::fit_sbs_canvas(s3d::pack_sbs(image,image,packing),output.width,output.height,
          packing,double(image.width)/image.height,fit==2?s3d::CanvasFit::Fill:s3d::CanvasFit::Fit);
        for(size_t i=0;i<expected.pixels.size();++i) {
          auto p=expected.pixels[i];
          check(std::abs(p.a-rendered[i*4+3])<1e-4f && std::abs(p.r-rendered[i*4+2])<1e-4f &&
            std::abs(p.g-rendered[i*4+1])<1e-4f && std::abs(p.b-rendered[i*4])<1e-4f,
            "Premiere input padding was duplicated into a gap between the eyes");
        }
      }
      world=original_world;
      std::cout<<"Premiere padded input: Half/Full Fit/Fill use image bounds without duplicated margins\n";
      params[3]->u.pd.value=1;params[5]->u.pd.value=1;
      rendered.resize(bgra.size());output=world;output.data=reinterpret_cast<PF_Pixel*>(rendered.data());
      if(argc==6&&std::string(argv[5])=="--sequence") {
        params[2]->u.pd.value=1;params[8]->u.fs_d.value=0;
        for(int repair:{2,3}) {
          params[37]->u.pd.value=repair;std::vector<double> timings;
          for(int n=0;n<40;++n) {
            int frame=(n%2)?39-n/2:n/2;in.current_time=frame;
            float marker=.1f+frame*.015f;
            for(size_t i=0;i<bgra.size();i+=4){bgra[i]=.2f;bgra[i+1]=.3f;bgra[i+2]=marker;bgra[i+3]=1;}
            auto start=std::chrono::steady_clock::now();
            auto error=entry(PF_Cmd_RENDER,&in,&out,params.data(),&output,nullptr);if(error)throw std::runtime_error(out.return_msg);
            timings.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
            for(size_t pixel:{size_t(image.width/4),size_t(image.width*3/4)})
              check(std::abs(rendered[pixel*4+2]-marker)<1e-5f,"Rendered frame repeated or arrived out of order");
          }
          std::sort(timings.begin(),timings.end());
          std::cout<<"Native sequence repair "<<repair<<" 40 seek-order frames preserved; ms median/p95/max "<<timings[20]<<'/'<<timings[38]<<'/'<<timings[39]<<'\n';
        }
      }
    }
    in.appl_id=0;check(entry(PF_Cmd_GLOBAL_SETUP,&in,&out,nullptr,nullptr,nullptr)!=0,"Unexpected non-Premiere host accepted");
    FreeLibrary(dll);std::cout<<"Plugin DLL, PiPL, SDK registration, controls and unbound render passed\n";return 0;
  } catch(const std::exception& e) {if(dll)FreeLibrary(dll);std::cerr<<e.what()<<'\n';return 1;}
}
