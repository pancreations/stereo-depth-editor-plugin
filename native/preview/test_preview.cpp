#include "SPBasic.h"
#include "PrSDKTransmit.h"
#include "PrSDKPPixSuite.h"
#include "preview_window.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <psapi.h>

namespace {
void check(bool v,const char* text){if(!v)throw std::runtime_error(text);}
struct Fake {int width=320,height=180,pitch=1280;unsigned pn=1,pd=1;PrPixelFormat format=PrPixelFormat_BGRA_4444_8u;std::vector<char> data;int disposed{};};
Fake& fake(PPixHand p){return *reinterpret_cast<Fake*>(p);}
prSuiteError release_frame(PPixHand p){++fake(p).disposed;return 0;}
prSuiteError pixels(PPixHand p,PrPPixBufferAccess,char** a){*a=fake(p).data.data();return 0;}
prSuiteError bounds(PPixHand p,prRect* r){*r={0,0,fake(p).width,fake(p).height};return 0;}
prSuiteError pitch(PPixHand p,csSDK_int32* n){*n=fake(p).pitch;return 0;}
prSuiteError par(PPixHand p,csSDK_uint32* n,csSDK_uint32* d){*n=fake(p).pn;*d=fake(p).pd;return 0;}
prSuiteError format(PPixHand p,PrPixelFormat* f){*f=fake(p).format;return 0;}
PrSDKPPixSuite suite{};SPBasicSuite basic{};int acquired{},released{};
SPErr acquire(const char* name,int version,const void** out){if(std::strcmp(name,kPrSDKPPixSuite)||version!=1)return -1;*out=&suite;++acquired;return 0;}
SPErr release(const char*,int){++released;return 0;}
SPBasicSuite* get_basic(){return &basic;}
HWND own_window(){
    HWND found{};
    EnumWindows([](HWND h,LPARAM value)->BOOL{
        DWORD pid{};GetWindowThreadProcessId(h,&pid);wchar_t name[128]{};GetClassNameW(h,name,128);
        if(pid==GetCurrentProcessId()&&!std::wcscmp(name,s3d::preview::window_class)){*reinterpret_cast<HWND*>(value)=h;return FALSE;}
        return TRUE;
    },reinterpret_cast<LPARAM>(&found));return found;
}
void wait_window(HWND h){SendMessageW(h,WM_NULL,0,0);RedrawWindow(h,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);Sleep(100);}
size_t private_bytes(){PROCESS_MEMORY_COUNTERS_EX m{};m.cb=sizeof(m);check(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m))!=0,"Cannot measure process memory");return m.PrivateUsage;}
void check_capture(HWND h) {
    RECT r{};GetClientRect(h,&r);HDC screen=GetDC(h),dc=CreateCompatibleDC(screen);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=r.right;info.bmiHeader.biHeight=-r.bottom;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void* data{};
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&data,nullptr,0);auto old=SelectObject(dc,bitmap);
    check(screen&&dc&&bitmap&&data&&old,"Capture DC allocation failed");
    BOOL ok=PrintWindow(h,dc,PW_CLIENTONLY);
    auto tl=GetPixel(dc,r.right/4,r.bottom/4),tr=GetPixel(dc,3*r.right/4,r.bottom/4),bl=GetPixel(dc,r.right/4,3*r.bottom/4);
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(h,screen);
    std::cout<<"Capture sample RGB: "<<std::hex<<tl<<", "<<tr<<", "<<bl<<std::dec<<" success "<<ok<<" size "<<r.right<<"x"<<r.bottom<<'\n';
    check(ok&&GetRValue(tl)>220&&GetGValue(tr)>220&&GetRValue(bl)>80&&GetRValue(bl)<120,"Captured pixels/orientation differ from transmitted frame");
}
}
int main(int argc,char** argv) {
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HMODULE dll{};tmStdParms stdpar{};tmInstance instance{};tmModule module{};
    try {
        check(argc>=2,"Supply transmitter plugin path [--visual]");
        for(double aspect:{16.0/9,32.0/9,9.0/16,2.39})for(unsigned edge=1;edge<=8;++edge){
            RECT r=s3d::preview::locked_rect({100,200,1000,750},edge,16,39,aspect);
            check(std::abs(double(r.right-r.left-16)/(r.bottom-r.top-39)-aspect)<0.025,"Resize distorted aspect");
        }
        auto fit=s3d::preview::fit_extent(1920,1080,32.0/9);check(fit.width==1920&&fit.height==540,"Full SBS fit wrong");
        char raw[24]={};raw[0]=11;raw[12]=22;
        auto positive=s3d::preview::copy_frame(raw,2,2,12,2,1);
        check(positive->bgra[0]==22&&positive->bgra[8]==11&&positive->aspect==2,"PPix bottom-up/padding/PAR conversion wrong");
        auto negative=s3d::preview::copy_frame(raw+12,2,2,-12,1,1);
        check(negative->bgra[0]==11&&negative->bgra[8]==22,"Negative PPix pitch wrong");
        {
            s3d::preview::FramePool pool;
            auto a=pool.copy(raw,2,2,12,1,1),b=pool.copy(raw,2,2,12,1,1),c=pool.copy(raw,2,2,12,1,1);
            check(!pool.copy(raw,2,2,12,1,1),"Preview pool grew beyond three busy frames");
            auto ptr=a->bgra.data();a.reset();auto reused=pool.copy(raw,2,2,12,1,1);
            check(reused->bgra.data()==ptr&&pool.allocated_bytes()==48,"Preview frame allocation was not reused");
        }
        dll=LoadLibraryA(argv[1]);check(dll!=nullptr,"Cannot load preview module");
        auto entry=reinterpret_cast<tmEntryFunc>(GetProcAddress(dll,"xTransmitEntry"));check(entry!=nullptr,"Missing transmitter entry");
        check(entry(1,kPrTrue,nullptr,&module)==tmResult_ErrorUnsupported,"Unsupported ABI accepted");
        check(entry(tmInterfaceVersion,kPrTrue,nullptr,&module)==0,"Module load failed");
        suite.Dispose=release_frame;suite.GetPixels=pixels;suite.GetBounds=bounds;suite.GetRowBytes=pitch;suite.GetPixelAspectRatio=par;suite.GetPixelFormat=format;
        basic.AcquireSuite=acquire;basic.ReleaseSuite=release;
        PlugUtilFuncs util{};util.getSPBasicSuite=get_basic;piSuites host{};host.utilFuncs=&util;stdpar.piSuites=&host;
        tmPluginInfo info{};check(module.Startup(&stdpar,&info)==0,"Startup failed");
        check(info.outVideoAvailable&&!info.outClockAvailable&&!info.outAudioAvailable&&!info.outVideoDefaultEnabled,"Incorrect device capabilities");
        check(module.CreateInstance(&stdpar,&instance)==0,"Instance creation failed");
        tmVideoMode mode{};check(module.QueryVideoMode(&stdpar,&instance,0,&mode)==0,"Video negotiation failed");
        check(mode.outPixelFormat==PrPixelFormat_BGRA_4444_8u&&mode.outWidth==0&&mode.outLatency==0,"Incorrect video format");
        Fake frame;frame.data.resize(size_t(frame.pitch)*frame.height);
        for(int y=0;y<frame.height;++y)for(int x=0;x<frame.width;++x){auto n=y*frame.pitch+x*4;frame.data[n+(x<frame.width/2?2:1)]=char(y<frame.height/2?100:240);frame.data[n+3]=char(255);}
        tmLabeledFrame frames[2]{};frames[0].inFrame=reinterpret_cast<PPixHand>(&frame);frames[1].inFrame=frames[0].inFrame;
        tmPushVideo video{};video.inFrames=frames;video.inFrameCount=1;
        check(module.PushVideo(&stdpar,&instance,&video)==0&&frame.disposed==1,"Inactive PPix not disposed");
        check(module.ActivateDeactivate(&stdpar,&instance,static_cast<PrActivationEvent>(0),kPrFalse,kPrTrue)==0,"Activation failed");
        check(module.PushVideo(&stdpar,&instance,&video)==0&&frame.disposed==2,"Active frame not delivered/disposed");
        HWND window=own_window();check(window!=nullptr,"Missing capture window");wait_window(window);
        check(IsWindowVisible(window)!=0,"Capture window not shown");
        check_capture(window);
        RECT client{};GetClientRect(window,&client);check(std::abs(double(client.right)/client.bottom-16.0/9)<.01,"Wrong client aspect");
        SendMessageW(window,WM_KEYDOWN,'B',0);check(!(GetWindowLongPtrW(window,GWL_STYLE)&WS_CAPTION),"Borderless failed");
        SendMessageW(window,WM_KEYDOWN,'T',0);wait_window(window);
        if(!(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST))
            std::cout<<"LIMITATION: this desktop did not retain WS_EX_TOPMOST; verify topmost behavior in Premiere\n";
        SendMessageW(window,WM_CLOSE,0,0);check(!IsWindowVisible(window),"Hide failed");
        check(module.PushVideo(&stdpar,&instance,&video)==0,"Hidden push failed");wait_window(window);check(!IsWindowVisible(window),"Frame reopened closed preview");
        module.SetupDialog(&stdpar,nullptr);wait_window(window);check(IsWindowVisible(window)!=0,"Setup did not reopen preview");
        frame.format=PrPixelFormat_VUYA_4444_8u;video.inFrameCount=2;int before=frame.disposed;
        check(module.PushVideo(&stdpar,&instance,&video)==tmResult_ErrorUnsupported&&frame.disposed==before+2,"Error path leaked PPix");
        frame.format=PrPixelFormat_BGRA_4444_8u;video.inFrameCount=1;
        // Resolution/aspect changes also occur on sequence switches and fractional playback.
        frame.width=640;frame.pitch=2560;frame.data.resize(size_t(frame.pitch)*frame.height);
        for(int y=0;y<frame.height;++y)for(int x=0;x<frame.width;++x){auto n=y*frame.pitch+x*4;frame.data[n]=char(y<frame.height/2?255:0);frame.data[n+1]=char(x<320?0:255);frame.data[n+2]=char(x<320?255:0);frame.data[n+3]=char(255);}
        check(module.PushVideo(&stdpar,&instance,&video)==0,"Full SBS push failed");wait_window(window);
        GetClientRect(window,&client);check(std::abs(double(client.right)/client.bottom-32.0/9)<.02,"Full SBS window aspect wrong");
        if(argc>2&&std::string(argv[2])=="--stress") {
            frame.width=3840;frame.height=2160;frame.pitch=frame.width*4;
            frame.data.assign(size_t(frame.pitch)*frame.height,char(120));
            auto batch=[&](int count){for(int n=0;n<count;++n){
                check(module.PushVideo(&stdpar,&instance,&video)==0,"Stress frame failed");
                if(n%40==0){SetWindowPos(window,nullptr,0,0,640+n%3*80,360+n%3*45,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);Sleep(2);}
                if(n%100==0){SendMessageW(window,WM_CLOSE,0,0);module.SetupDialog(&stdpar,nullptr);}
            }wait_window(window);};
            batch(200);const auto warm=private_bytes();batch(600);const auto middle=private_bytes();batch(600);const auto last=private_bytes();
            std::cout<<"4K preview stress private bytes warm/middle/end: "<<warm<<'/'<<middle<<'/'<<last<<'\n';
            check(last<=middle+size_t(64)*1024*1024,"Preview memory kept growing after warm-up");
        }
        if(argc>2&&std::string(argv[2])=="--visual") {std::cout<<"Preview visible for 20 seconds\n"<<std::flush;Sleep(20000);}
        check(module.DisposeInstance(&stdpar,&instance)==0,"Dispose instance failed");
        check(module.Shutdown(&stdpar)==0,"Shutdown failed");check(acquired==released,"Adobe suite leak");
        check(!own_window(),"Preview thread/window survived shutdown");
        entry(tmInterfaceVersion,kPrFalse,nullptr,&module);FreeLibrary(dll);
        std::cout<<"SBS Preview geometry, PPix ownership/orientation, ABI, window controls and lifecycle passed\n";return 0;
    }catch(const std::exception& e){
        if(instance.ioPrivateInstanceData&&module.DisposeInstance)module.DisposeInstance(&stdpar,&instance);
        if(stdpar.ioPrivatePluginData&&module.Shutdown)module.Shutdown(&stdpar);
        if(dll)FreeLibrary(dll);std::cerr<<e.what()<<'\n';return 1;
    }
}
