// Premiere-only adapter. Adobe's shared effect ABI is not an After Effects target.
// SDK-dependent: compile and complete docs/RELEASE_CHECKLIST.md before distribution.
#include "AEConfig.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "AE_EffectPixelFormat.h"
#include "PrSDKPixelFormat.h"
#include "SPBasic.h"
#include "PrSDKAESupport.h"
#include "s3d/pipeline.hpp"
#include "depth_bridge.hpp"
#include "runtime_config.hpp"
#include "../gpu/runtime_renderer.hpp"
#include "PrSDKStringSuite.h"
#include "AE_EffectSuites.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <map>
#include <mutex>

namespace {
enum Param {
  Input,OutputGroup,Preview,Output,Order,Fit,OutputGroupEnd,StereoGroup,Strength,Convergence,MaxParallax,EyeSeparation,Direction,StereoGroupEnd,SourceGroup,Model,Quality,Override,Browse,ExternalId,SourceGroupEnd,MapGroup,Invert,Fidelity,Contrast,Gamma,Near,Far,Offset,Scale,Clamp,Normalize,Interpretation,Blur,EdgePreserve,MapGroupEnd,RepairGroup,Repair,Radius,Softness,Artifact,RepairGroupEnd,CacheGroup,Slot,FrameOffset,CacheGroupEnd,Count
};
PF_Err parameters(PF_InData* in_data,PF_OutData* out_data) {
  PF_Err err=PF_Err_NONE;PF_ParamDef def{};
  def={};PF_ADD_TOPICX("OUTPUT AND PREVIEW",0,100);
  def={};PF_ADD_POPUP("Preview",8,1,"Final Stereo (SBS)|Original|Depth Map|Left Eye|Right Eye|Stereo Difference|Parallax Visualization|Occlusion Mask",28);
  def={};PF_ADD_POPUP("Output",2,1,"Half SBS|Full SBS",26);
  def={};PF_ADD_POPUP("Eye Order",2,1,"Left / Right|Right / Left",27);
  def={};PF_ADD_POPUP("Canvas Fit",2,1,"Fit (show whole image)|Fill (crop edges)",34);
  def={};PF_END_TOPIC(101);
  def={};PF_ADD_TOPICX("STEREO",0,102);
  def={};PF_ADD_FLOAT_SLIDERX("Stereo Depth Strength",0,100,0,4,1,2,0,0,17);
  def={};PF_ADD_FLOAT_SLIDERX("Convergence",-10,10,0,1,.5,3,0,0,18);
  def={};PF_ADD_FLOAT_SLIDERX("Maximum Parallax (%)",0,100,0,5,3,2,0,0,19);
  def={};PF_ADD_FLOAT_SLIDERX("Eye Separation (%)",0,100,0,5,2,2,0,0,20);
  def={};PF_ADD_POPUP("Depth Direction",2,1,"Normal|Reverse",21);
  def={};PF_END_TOPIC(103);
  def={};PF_ADD_TOPICX("DEPTH SOURCE",0,104);
  def={};PF_ADD_POPUP("AI Depth Model",17,3,"DA-V2 Small|DA-V2 Base|DA-V2 Large|DA-V2 Small FP32|Video DA Small|Video DA Small Metric|Video DA Base|Video DA Base Metric|Video DA Large|Video DA Large Metric|DA3 Small|DA3 Base|DA3 Large 1.1|DA3 Giant 1.1|DA3 Mono Large|DA3 Metric Large|DA3 Nested Giant-Large",30);
  def={};PF_ADD_POPUP("Inference Quality",4,2,"Preview|Balanced|High|Maximum",31);
  def={};PF_ADD_CHECKBOX("Depth Pass Override","Use external depth",FALSE,0,2);
  def={};PF_ADD_BUTTON("External Depth Source","Browse Depth Source...",0,PF_ParamFlag_SUPERVISE,32);
  def={};def.ui_flags=PF_PUI_INVISIBLE;PF_ADD_SLIDER("External Depth Source ID",0,999999,0,999999,0,33);
  def={};PF_END_TOPIC(105);
  def={};PF_ADD_TOPICX("DEPTH MAP",PF_ParamFlag_START_COLLAPSED,106);
  def={};PF_ADD_CHECKBOX("Invert Depth Map","Invert",FALSE,0,13);
  def={};PF_ADD_FLOAT_SLIDERX("Depth Fidelity",0,100,0,100,100,1,0,0,4);
  def={};PF_ADD_FLOAT_SLIDERX("Depth Contrast",0,10,0,3,1,2,0,0,5);
  def={};PF_ADD_FLOAT_SLIDERX("Depth Gamma",0.05,10,0.1,3,1,2,0,0,6);
  def={};PF_ADD_FLOAT_SLIDERX("Near Point",-1000000,1000000,0,1,1,3,0,0,7);
  def={};PF_ADD_FLOAT_SLIDERX("Far Point",-1000000,1000000,0,1,0,3,0,0,8);
  def={};PF_ADD_FLOAT_SLIDERX("Depth Offset",-10,10,-1,1,0,3,0,0,9);
  def={};PF_ADD_FLOAT_SLIDERX("Depth Scale",-10,10,0,3,1,3,0,0,10);
  def={};PF_ADD_CHECKBOX("Clamp Depth","Clamp to usable range",TRUE,0,11);
  def={};PF_ADD_CHECKBOX("Normalize Depth","Use fixed cache range",TRUE,0,12);
  def={};PF_ADD_POPUP("Depth Interpretation",2,1,"White = Near|Black = Near",14);
  def={};PF_ADD_FLOAT_SLIDERX("Depth Blur",0,16,0,8,0,1,0,0,15);
  def={};PF_ADD_FLOAT_SLIDERX("Edge Preservation",0,100,0,100,80,1,0,0,16);
  def={};PF_END_TOPIC(107);
  def={};PF_ADD_TOPICX("EDGE RECONSTRUCTION",PF_ParamFlag_START_COLLAPSED,108);
  def={};PF_ADD_POPUP("Edge Reconstruction",3,2,"Off (original fallback)|Fast (stretch)|High (content-aware)",22);
  def={};PF_ADD_SLIDER("Reconstruction Radius",1,512,1,128,32,23);
  def={};PF_ADD_FLOAT_SLIDERX("Edge Softness",0,100,0,100,20,1,0,0,24);
  def={};PF_ADD_FLOAT_SLIDERX("Artifact Reduction",0,100,0,100,70,1,0,0,25);
  def={};PF_END_TOPIC(109);
  def={};PF_ADD_TOPICX("PREPARED CACHE (OPTIONAL)",PF_ParamFlag_START_COLLAPSED,110);
  def={};PF_ADD_SLIDER("Depth Cache Slot",0,999999,0,1000,0,1);
  def={};PF_ADD_SLIDER("Depth Frame Offset",-1000000,1000000,-100,100,0,3);
  def={};PF_END_TOPIC(111);
  out_data->num_params=Count;return err;
}
class PixelSuite {
  SPBasicSuite* basic;
public:
  const PF_PixelFormatSuite1* suite=nullptr;
  explicit PixelSuite(PF_InData* in):basic(in->pica_basicP) {
    if(basic->AcquireSuite(kPFPixelFormatSuite,kPFPixelFormatSuiteVersion1,
                          reinterpret_cast<const void**>(&suite)) || !suite)
      throw std::runtime_error("Premiere pixel format suite unavailable");
  }
  ~PixelSuite() {basic->ReleaseSuite(kPFPixelFormatSuite,kPFPixelFormatSuiteVersion1);}
};
std::filesystem::path cache_root() {
  return s3d::cache_root();
}
struct BGRA {float b,g,r,a;};
std::string source_identity(PF_InData* in) {
  const PF_UtilitySuite* utility=nullptr;const PrSDKStringSuite* strings=nullptr;
  auto* basic=in->pica_basicP;PrSDKString value{};
  std::string result="effect:"+std::to_string(reinterpret_cast<std::uintptr_t>(in->effect_ref));
  if(!basic->AcquireSuite(kPFUtilitySuite,kPFUtilitySuiteVersion9,reinterpret_cast<const void**>(&utility))&&utility) {
    if(!basic->AcquireSuite(kPrSDKStringSuite,kPrSDKStringSuiteVersion,reinterpret_cast<const void**>(&strings))&&strings) {
      if(!utility->GetFileName(in->effect_ref,&value)) {
        csSDK_uint32 size=32768;std::vector<char> text(size);
        if(!strings->CopyToUTF8String(&value,reinterpret_cast<prUTF8Char*>(text.data()),&size)) result=text.data();
        strings->DisposeString(&value);
      }
      basic->ReleaseSuite(kPrSDKStringSuite,kPrSDKStringSuiteVersion);
    }
    basic->ReleaseSuite(kPFUtilitySuite,kPFUtilitySuiteVersion9);
  }
  return result;
}
void update_source_ui(PF_InData* in,PF_ParamDef* params[]) {
  static std::mutex mutex;static std::map<int,std::string> names;
  std::lock_guard guard(mutex);int id=params[ExternalId]->u.sd.value;
  if(!names.count(id))names[id]=s3d::premiere::external_source_name(id);
  const PF_ParamUtilsSuite3* suite=nullptr;
  if(!in->pica_basicP->AcquireSuite(kPFParamUtilsSuite,kPFParamUtilsSuiteVersion3,reinterpret_cast<const void**>(&suite))&&suite) {
    PF_ParamDef def=*params[Browse];
    std::snprintf(def.PF_DEF_NAME,sizeof(def.PF_DEF_NAME),"%s",names[id].c_str());
    suite->PF_UpdateParamUI(in->effect_ref,Browse,&def);
    in->pica_basicP->ReleaseSuite(kPFParamUtilsSuite,kPFParamUtilsSuiteVersion3);
  }
}
s3d::Image read_world(const PF_LayerDef& world) {
  s3d::Image out(world.width,world.height);
  for(int y=0;y<world.height;++y) {
    auto row=reinterpret_cast<const BGRA*>(reinterpret_cast<const char*>(world.data)+std::ptrdiff_t(y)*world.rowbytes);
    for(int x=0;x<world.width;++x) out.at(x,y)={row[x].r,row[x].g,row[x].b,row[x].a};
  }
  return out;
}
void write_world(const s3d::Image& image,PF_LayerDef& world) {
  if(image.width!=world.width || image.height!=world.height) throw std::runtime_error("Unexpected Premiere output dimensions");
  for(int y=0;y<world.height;++y) {
    auto row=reinterpret_cast<BGRA*>(reinterpret_cast<char*>(world.data)+std::ptrdiff_t(y)*world.rowbytes);
    for(int x=0;x<world.width;++x) {auto p=image.at(x,y);row[x]={p.b,p.g,p.r,p.a};}
  }
}
PF_Err render_frame(PF_InData* in,PF_ParamDef* p[],PF_LayerDef* output) {
  // Adobe may render many frames concurrently. Backpressure before full-size
  // copies prevents waiting inference requests from each retaining a float frame.
  static std::timed_mutex frame_memory_mutex;
  auto cancelled=[&]{return in->inter.abort&&PF_ABORT(in)!=0;};
  std::unique_lock memory_guard(frame_memory_mutex,std::defer_lock);
  while(!memory_guard.try_lock_for(std::chrono::milliseconds(25)))if(cancelled())return PF_Interrupt_CANCEL;
  if(cancelled())return PF_Interrupt_CANCEL;
  PixelSuite pixels(in);PrPixelFormat input_format{},output_format{};
  if(pixels.suite->GetPixelFormat(&p[Input]->u.ld,&input_format) ||
     pixels.suite->GetPixelFormat(output,&output_format) ||
     input_format!=PrPixelFormat_BGRA_4444_32f || output_format!=PrPixelFormat_BGRA_4444_32f)
    throw std::runtime_error("StereoDepth requires Premiere BGRA 32-bit float frames");
  if(in->field!=PF_Field_FRAME) throw std::runtime_error("Use a progressive sequence; interlaced rendering is not supported yet");
  auto source=read_world(p[Input]->u.ld);
  const auto fit=p[Fit]->u.pd.value==2?s3d::CanvasFit::Fill:s3d::CanvasFit::Fit;
  if(p[Preview]->u.pd.value==2) {
    if(source.width!=output->width || source.height!=output->height)
      source=s3d::fit_canvas(source,output->width,output->height,fit);
    write_world(source,*output);return PF_Err_NONE;
  }
  // Premiere can deliver the result of Motion at sequence size, with a smaller
  // clip surrounded by zero-alpha canvas. Fit the picture, not those margins:
  // otherwise they are duplicated at the outer edges and at the SBS seam.
  // Scan actual alpha because the host's extent hint may be empty or full-frame.
  source=s3d::trim_transparent_canvas(std::move(source));
  s3d::CacheBinding binding{};s3d::Depth depth;
  if(p[Slot]->u.sd.value>0) {
    const auto root=cache_root();binding=s3d::read_binding(root,p[Slot]->u.sd.value);
    if(binding.external!=(p[Override]->u.bd.value!=0))
      throw std::runtime_error("Depth Pass Override does not match the prepared cache");
    auto frame=s3d::frame_at_time(in->current_time,in->time_scale,binding.fps_num,binding.fps_den)+p[FrameOffset]->u.sd.value;
    depth=s3d::read_depth_cache(s3d::cache_frame_path(root,binding,frame));
  } else {
    s3d::premiere::PreparedDepth prepared;
    if(p[Override]->u.bd.value) {
      if(!p[ExternalId]->u.sd.value)throw std::runtime_error("No external depth selected. Click Browse Depth Source in DEPTH SOURCE.");
      prepared=s3d::premiere::external_depth(p[ExternalId]->u.sd.value,in->current_time,in->time_scale,
        p[FrameOffset]->u.sd.value,in->time_scale,in->time_step,cancelled);
      binding.width=prepared.depth.width;binding.height=prepared.depth.height;
    } else {
      prepared=s3d::premiere::automatic_depth(source,p[Model]->u.pd.value-1,p[Quality]->u.pd.value-1,source_identity(in),cancelled);
      binding.width=source.width;binding.height=source.height;
    }
    depth=std::move(prepared.depth);binding.low=prepared.low;binding.high=prepared.high;
  }
  s3d::Settings s;
  auto f=[&](Param id) {return float(p[id]->u.fs_d.value);};
  s.depth.fidelity=f(Fidelity)/100;s.depth.contrast=f(Contrast);s.depth.gamma=f(Gamma);
  s.depth.near_point=f(Near);s.depth.far_point=f(Far);s.depth.offset=f(Offset);s.depth.scale=f(Scale);
  s.depth.clamp=p[Clamp]->u.bd.value!=0;s.depth.normalize=p[Normalize]->u.bd.value!=0;
  s.depth.invert=p[Invert]->u.bd.value!=0;s.depth.white_near=p[Interpretation]->u.pd.value==1;
  s.depth.normalization_low=binding.low;s.depth.normalization_high=binding.high;
  s.depth.blur=f(Blur);s.depth.edge_preservation=f(EdgePreserve)/100;
  s.stereo.strength=f(Strength);s.stereo.convergence=f(Convergence);
  s.stereo.max_parallax_percent=f(MaxParallax);s.stereo.eye_separation_percent=f(EyeSeparation);
  s.stereo.reverse_depth=p[Direction]->u.pd.value==2;
  s.repair.mode=static_cast<s3d::Reconstruction>(p[Repair]->u.pd.value-1);
  s.repair.radius=p[Radius]->u.sd.value;s.repair.softness=f(Softness)/100;s.repair.artifact_reduction=f(Artifact)/100;
  s.output=p[Output]->u.pd.value==2?s3d::OutputMode::FullSBS:s3d::OutputMode::HalfSBS;
  s.right_first=p[Order]->u.pd.value==2;s.preview=static_cast<s3d::Preview>(p[Preview]->u.pd.value-1);
  // External/prepared depth describes the picture, excluding host canvas padding.
  // Automatic depth uses the same unpadded picture. Fit both resulting eyes alike.
  if(cancelled()) return PF_Interrupt_CANCEL;
#if defined(_WIN32)
  const auto* backend=std::getenv("S3D_STEREO_DEVICE");
  auto result=backend && std::string(backend)=="cpu" ? s3d::render(source,depth,s).output : s3d::gpu::render_cuda(source,depth,s);
#else
  auto result=s3d::render(source,depth,s).output;
#endif
  if(s.preview==s3d::Preview::Final)
    result=s3d::fit_sbs_canvas(result,output->width,output->height,s.output,double(source.width)/source.height,fit);
  else if(result.width!=output->width || result.height!=output->height)
    result=s3d::fit_canvas(result,output->width,output->height,fit);
  write_world(result,*output);return PF_Err_NONE;
}
}

#ifdef _WIN32
#define S3D_EXPORT __declspec(dllexport)
#else
#define S3D_EXPORT __attribute__((visibility("default")))
#endif
extern "C" S3D_EXPORT PF_Err EffectMain(PF_Cmd cmd,PF_InData* in_data,PF_OutData* out_data,
                                                    PF_ParamDef* params[],PF_LayerDef* output,void* extra) {
  try {
    switch(cmd) {
      case PF_Cmd_ABOUT:
        std::snprintf(out_data->return_msg,sizeof(out_data->return_msg),"StereoDepth 0.3.1 — Premiere development build");break;
      case PF_Cmd_GLOBAL_SETUP: {
        // FourCC expressed numerically to avoid compiler-specific multichar literal handling.
        if(in_data->appl_id!=0x50724D72) return PF_Err_INVALID_CALLBACK;
        out_data->my_version=131073;out_data->out_flags=0;out_data->out_flags2=PF_OutFlag2_PARAM_GROUP_START_COLLAPSED_FLAG;
        PixelSuite pixels(in_data);
        auto err=pixels.suite->ClearSupportedPixelFormats(in_data->effect_ref);if(err) return err;
        return pixels.suite->AddSupportedPixelFormat(in_data->effect_ref,PrPixelFormat_BGRA_4444_32f);
      }
      case PF_Cmd_PARAMS_SETUP:return parameters(in_data,out_data);
      case PF_Cmd_RENDER:return render_frame(in_data,params,output);
      case PF_Cmd_USER_CHANGED_PARAM: {
        const auto* changed=static_cast<PF_UserChangedParamExtra*>(extra);
        if(changed && changed->param_index==Browse) {
          std::string name;int id=s3d::premiere::browse_depth_source(name);
          if(id) {
            params[ExternalId]->u.sd.value=id;params[ExternalId]->uu.change_flags|=PF_ChangeFlag_CHANGED_VALUE;
            params[Override]->u.bd.value=TRUE;params[Override]->uu.change_flags|=PF_ChangeFlag_CHANGED_VALUE;
            params[Slot]->u.sd.value=0;params[Slot]->uu.change_flags|=PF_ChangeFlag_CHANGED_VALUE;
            std::snprintf(out_data->return_msg,sizeof(out_data->return_msg),"Depth source selected: %s",name.c_str());
            update_source_ui(in_data,params);
            out_data->out_flags|=PF_OutFlag_FORCE_RERENDER|PF_OutFlag_REFRESH_UI;
          }
        }
        break;
      }
      case PF_Cmd_UPDATE_PARAMS_UI:update_source_ui(in_data,params);break;
      default:break;
    }
    return PF_Err_NONE;
  } catch(const s3d::premiere::RenderCancelled&) {return PF_Interrupt_CANCEL;}
    catch(const std::bad_alloc&) {return PF_Err_OUT_OF_MEMORY;}
    catch(const std::exception& e) {
      std::snprintf(out_data->return_msg,sizeof(out_data->return_msg),"2D to 3D: %s",e.what());
      out_data->out_flags|=PF_OutFlag_DISPLAY_ERROR_MESSAGE;
      return PF_Err_INTERNAL_STRUCT_DAMAGED;
    } catch(...) {return PF_Err_INTERNAL_STRUCT_DAMAGED;}
}
