#include "SPBasic.h"
#include "PrSDKTransmit.h"
#include "PrSDKPPixSuite.h"
#include "preview_window.hpp"
#include <cstring>
#include <memory>
#include <stdexcept>

namespace {
struct Plugin {
    SPBasicSuite* basic{};
    const PrSDKPPixSuite* pixels{};
    std::unique_ptr<s3d::preview::Window> window;
    s3d::preview::FramePool frames;
    explicit Plugin(tmStdParms* p) {
        if(!p||!p->piSuites||!p->piSuites->utilFuncs)throw std::runtime_error("Missing Adobe suites");
        basic=p->piSuites->utilFuncs->getSPBasicSuite();
        if(!basic||basic->AcquireSuite(kPrSDKPPixSuite,kPrSDKPPixSuiteVersion,reinterpret_cast<const void**>(&pixels))||!pixels)
            throw std::runtime_error("Missing Adobe pixel suite");
    }
    s3d::preview::Window& preview() {
        if(!window) {
            HMODULE module{};
            if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&module_anchor),&module))throw std::runtime_error("Missing plugin module");
            window=std::make_unique<s3d::preview::Window>(module);
        }
        return *window;
    }
    static void module_anchor(){}
    ~Plugin(){window.reset();if(pixels)basic->ReleaseSuite(kPrSDKPPixSuite,kPrSDKPPixSuiteVersion);}
};
struct Instance {bool active{};};
Plugin& plugin(const tmStdParms* p){return *static_cast<Plugin*>(p->ioPrivatePluginData);}
template<class F> tmResult guard(F f)noexcept {
    try{return f();}catch(const std::bad_alloc&){return tmResult_ErrorMemory;}
    catch(...){OutputDebugStringW(L"StereoDepth SBS Preview: transmit callback failed\n");return tmResult_ErrorUnknown;}
}
tmResult startup(tmStdParms* p,tmPluginInfo* info) {
    return guard([&]{
        if(!p||!info)return tmResult_ErrorInvalidArgument;
        if(p->inPluginIndex!=0)return tmResult_ErrorUnsupported;
        auto owned=std::make_unique<Plugin>(p);*info={};
        std::strcpy(info->outIdentifier.mGUID,"6B6F2360-A843-4F7C-85B2-394BF46D3269");
        constexpr wchar_t name[]=L"StereoDepth SBS Preview";
        static_assert(sizeof(prUTF16Char)==sizeof(wchar_t));std::memcpy(info->outDisplayName,name,sizeof(name));
        info->outInterfaceVersion=tmInterfaceVersion;
        info->outVideoAvailable=kPrTrue;info->outVideoDefaultEnabled=kPrFalse;
        info->outHasSetup=kPrTrue; // Video-only: Premiere owns audio and playback clock.
        p->ioPrivatePluginData=owned.release();return tmResult_Success;
    });
}
tmResult shutdown(tmStdParms* p){return guard([&]{delete static_cast<Plugin*>(p->ioPrivatePluginData);p->ioPrivatePluginData=nullptr;return tmResult_Success;});}
tmResult setup(tmStdParms* p,prParentWnd){return guard([&]{plugin(p).preview().show();return tmResult_Success;});}
tmResult reset(const tmStdParms*,prBool* out){if(!out)return tmResult_ErrorInvalidArgument;*out=kPrFalse;return tmResult_Success;}
tmResult create(const tmStdParms*,tmInstance* i){return guard([&]{if(!i)return tmResult_ErrorInvalidArgument;i->ioPrivateInstanceData=new Instance;return tmResult_Success;});}
tmResult dispose(const tmStdParms*,tmInstance* i){return guard([&]{delete static_cast<Instance*>(i->ioPrivateInstanceData);i->ioPrivateInstanceData=nullptr;return tmResult_Success;});}
tmResult query(const tmStdParms*,const tmInstance*,csSDK_int32 iteration,tmVideoMode* mode) {
    if(iteration!=0)return tmResult_ErrorUnsupported;
    if(!mode)return tmResult_ErrorInvalidArgument;
    *mode={};mode->outPixelFormat=PrPixelFormat_BGRA_4444_8u;mode->outFieldType=prFieldsNone;
    auto& color=mode->outColorSpaceRec;color.outColorSpaceType=kPrSDKColorSpaceType_SEITags;
    auto& tags=color.outSEICodesRec;
    tags.colorPrimariesCode=static_cast<int>(PrColorPrimaries::kBT709);
    tags.transferCharacteristicCode=static_cast<int>(PrTransferCharacteristic::kIEC61966_2_1);
    tags.matrixEquationsCode=static_cast<int>(PrMatrixEquations::kIdentity);
    tags.isFullRange=true;tags.isRGB=true;tags.bitDepth=static_cast<int>(PrEncodingBitDepth::k8u);
    return tmResult_Success;
}
tmResult activate(const tmStdParms* p,const tmInstance* i,PrActivationEvent,prBool,prBool video) {
    return guard([&]{
        if(!i||!i->ioPrivateInstanceData)return tmResult_ErrorInvalidArgument;
        static_cast<Instance*>(i->ioPrivateInstanceData)->active=video!=0;
        if(video)plugin(p).preview();
        return tmResult_Success;
    });
}
tmResult push(const tmStdParms* p,const tmInstance* i,const tmPushVideo* video) {
    return guard([&]{
        if(!video||(video->inFrameCount&&!video->inFrames))return tmResult_ErrorInvalidArgument;
        const auto* pixels=plugin(p).pixels;
        // Every delivered PPix is transferred to us, even for inactive instances/errors.
        struct Dispose {const PrSDKPPixSuite* p;const tmPushVideo* v;~Dispose(){for(size_t n=0;n<v->inFrameCount;++n)if(v->inFrames[n].inFrame)p->Dispose(v->inFrames[n].inFrame);}} release{pixels,video};
        if(!i||!i->ioPrivateInstanceData)return tmResult_ErrorInvalidArgument;
        if(!static_cast<Instance*>(i->ioPrivateInstanceData)->active||!video->inFrameCount)return tmResult_Success;
        auto frame=video->inFrames[0].inFrame;prRect bounds{};PrPixelFormat format{};char* address{};csSDK_int32 pitch{};csSDK_uint32 pn{},pd{};
        if(pixels->GetPixelFormat(frame,&format)||format!=PrPixelFormat_BGRA_4444_8u)return tmResult_ErrorUnsupported;
        if(pixels->GetBounds(frame,&bounds)||pixels->GetRowBytes(frame,&pitch)||pixels->GetPixelAspectRatio(frame,&pn,&pd)||
            pixels->GetPixels(frame,PrPPixBufferAccess_ReadOnly,&address))return tmResult_ErrorUnknown;
        auto copied=plugin(p).frames.copy(address,bounds.right-bounds.left,bounds.bottom-bounds.top,pitch,pn,pd);
        if(copied)plugin(p).preview().submit(std::move(copied));return tmResult_Success;
    });
}
}
extern "C" __declspec(dllexport) tmResult xTransmitEntry(csSDK_int32 version,prBool load,piSuitesPtr,tmModule* module) {
    if(!load)return tmResult_Success;
    if(version<tmInterfaceVersion||!module)return tmResult_ErrorUnsupported;
    *module={};module->Startup=startup;module->Shutdown=shutdown;module->SetupDialog=setup;module->NeedsReset=reset;
    module->CreateInstance=create;module->DisposeInstance=dispose;module->QueryVideoMode=query;
    module->ActivateDeactivate=activate;module->PushVideo=push;
    return tmResult_Success;
}
