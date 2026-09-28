#include "preview_window.hpp"
#include <d2d1.h>
#include <windowsx.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace s3d::preview {
using Microsoft::WRL::ComPtr;
constexpr UINT frame_message=WM_APP+1, stop_message=WM_APP+2;
constexpr DWORD decorated=WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX;
struct Window::Impl {
    HINSTANCE module;
    std::atomic<HWND> hwnd{};
    std::thread thread;
    std::mutex mutex;
    std::shared_ptr<Frame> pending, current;
    bool posted{}, borderless{}, topmost{}, first_frame=true, user_hidden{};
    double aspect=16.0/9.0;
    ComPtr<ID2D1Factory> factory;
    ComPtr<ID2D1HwndRenderTarget> target;
    ComPtr<ID2D1Bitmap> bitmap;
    bool upload=true;
    ULONGLONG retry_after{};
    void display_failed(HWND h) {
        bitmap.Reset();target.Reset();retry_after=GetTickCount64()+1000;
        SetTimer(h,1,1000,nullptr); // Bound device-loss/OOM retries; never repaint-spin.
    }
    explicit Impl(HINSTANCE m):module(m) {}
    static LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp) noexcept {
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(msg==WM_NCCREATE) {
            self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); self->hwnd=h;
        }
        try { if(self)return self->message(h,msg,wp,lp); }
        catch(...) { OutputDebugStringW(L"StereoDepth SBS Preview: window operation failed\n"); }
        return DefWindowProcW(h,msg,wp,lp);
    }
    void dimensions(int& bw,int& bh) {
        if(borderless){bw=0;bh=0;return;}
        RECT r{}; AdjustWindowRectExForDpi(&r,DWORD(GetWindowLongPtrW(hwnd,GWL_STYLE)),FALSE,
            DWORD(GetWindowLongPtrW(hwnd,GWL_EXSTYLE)),GetDpiForWindow(hwnd));
        bw=r.right-r.left; bh=r.bottom-r.top;
    }
    void resize_client(int width,int height) {
        int bw,bh; dimensions(bw,bh);
        SetWindowPos(hwnd,nullptr,0,0,width+bw,height+bh,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    }
    void fit_monitor() {
        MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&mi);
        auto size=fit_extent(std::max(100L,mi.rcWork.right-mi.rcWork.left-100),
                             std::max(100L,mi.rcWork.bottom-mi.rcWork.top-100),aspect);
        resize_client(size.width,size.height);
        SetWindowPos(hwnd,nullptr,mi.rcWork.left+40,mi.rcWork.top+40,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    }
    void toggle_border() {
        RECT client{};GetClientRect(hwnd,&client);borderless=!borderless;
        auto visible=GetWindowLongPtrW(hwnd,GWL_STYLE)&WS_VISIBLE;
        SetWindowLongPtrW(hwnd,GWL_STYLE,(borderless?WS_POPUP|WS_THICKFRAME:decorated)|visible);
        SetWindowPos(hwnd,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
        resize_client(client.right,client.bottom);
    }
    void command(unsigned id) {
        switch(id) {
        case 1:toggle_border();break;
        case 2:topmost=!topmost;{
            auto style=GetWindowLongPtrW(hwnd,GWL_EXSTYLE);
            SetWindowLongPtrW(hwnd,GWL_EXSTYLE,topmost?style|WS_EX_TOPMOST:style&~WS_EX_TOPMOST);
            SetWindowPos(hwnd,topmost?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_FRAMECHANGED);}break;
        case 3:fit_monitor();break;
        case 4:case 5:
            if(current) { int h=std::max(1,current->height/(id==4?2:1));resize_client(int(std::lround(h*aspect)),h); }break;
        case 6: user_hidden=true;ShowWindow(hwnd,SW_HIDE);break;
        }
    }
    void paint(HWND h) {
        const auto frame=current; // Hold pixels across re-entrant Windows/COM calls.
        PAINTSTRUCT ps{};BeginPaint(h,&ps);
        struct End {HWND h;PAINTSTRUCT* p;~End(){EndPaint(h,p);}} end{h,&ps};
        RECT rc{};GetClientRect(h,&rc);if(rc.right<1||rc.bottom<1)return;
        if(GetTickCount64()<retry_after)return;
        if(!target) {
            auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);
            if(FAILED(factory->CreateHwndRenderTarget(props,D2D1::HwndRenderTargetProperties(h,
                D2D1::SizeU(rc.right,rc.bottom),D2D1_PRESENT_OPTIONS_IMMEDIATELY),&target))) {
                FillRect(ps.hdc,&rc,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
                SetTextColor(ps.hdc,RGB(255,255,255));SetBkMode(ps.hdc,TRANSPARENT);
                DrawTextW(ps.hdc,L"SBS Preview could not initialize its display. Reopen the preview.",-1,&rc,DT_CENTER|DT_VCENTER|DT_SINGLELINE);return;
            }
            bitmap.Reset();upload=true;
        }
        if(frame && upload) {
            if(bitmap && (bitmap->GetPixelSize().width!=unsigned(frame->width)||bitmap->GetPixelSize().height!=unsigned(frame->height))) bitmap.Reset();
            HRESULT hr;
            if(!bitmap) hr=target->CreateBitmap(D2D1::SizeU(frame->width,frame->height),frame->bgra.data(),frame->width*4,
                D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96),&bitmap);
            else hr=bitmap->CopyFromMemory(nullptr,frame->bgra.data(),frame->width*4);
            if(FAILED(hr)) {display_failed(h);return;}
            upload=false;
        }
        target->BeginDraw();target->Clear(D2D1::ColorF(D2D1::ColorF::Black));
        if(bitmap) {
            auto size=fit_extent(rc.right,rc.bottom,aspect);
            float x=float(rc.right-size.width)/2,y=float(rc.bottom-size.height)/2;
            target->DrawBitmap(bitmap.Get(),D2D1::RectF(x,y,x+size.width,y+size.height),1,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
        auto result=target->EndDraw();
        if(FAILED(result))display_failed(h);
        if(!current) {
            SetTextColor(ps.hdc,RGB(210,210,210));SetBkMode(ps.hdc,TRANSPARENT);
            DrawTextW(ps.hdc,L"SBS Preview\nEnable StereoDepth SBS Preview in Premiere Playback preferences,\nthen scrub or play the timeline.\nRight-click for window controls.",-1,&rc,DT_CENTER|DT_WORDBREAK);
        }
    }
    void capture(HDC dc) {
        const auto frame=current;
        RECT r{};GetClientRect(hwnd,&r);
        FillRect(dc,&r,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        if(!frame||r.right<1||r.bottom<1)return;
        auto size=fit_extent(r.right,r.bottom,aspect);
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=frame->width;info.bmiHeader.biHeight=-frame->height;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        int saved=SaveDC(dc);SetStretchBltMode(dc,HALFTONE);SetBrushOrgEx(dc,0,0,nullptr);
        StretchDIBits(dc,(r.right-size.width)/2,(r.bottom-size.height)/2,size.width,size.height,
            0,0,frame->width,frame->height,frame->bgra.data(),&info,DIB_RGB_COLORS,SRCCOPY);
        RestoreDC(dc,saved);
    }
    LRESULT message(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
        switch(msg) {
        case frame_message: {
            {std::lock_guard lock(mutex);current=std::move(pending);posted=false;}
            if(!current)return 0;
            bool changed=std::abs(aspect-current->aspect)>0.0001;aspect=current->aspect;upload=true;
            if(first_frame||changed){RECT r{};GetClientRect(h,&r);auto size=fit_extent(r.right,std::max(r.bottom,1000L),aspect);resize_client(size.width,size.height);}
            if(first_frame&&!user_hidden)ShowWindow(h,SW_SHOWNOACTIVATE);
            first_frame=false;InvalidateRect(h,nullptr,FALSE);return 0;
        }
        case show_message:user_hidden=false;ShowWindow(h,IsIconic(h)?SW_RESTORE:SW_SHOWNOACTIVATE);InvalidateRect(h,nullptr,FALSE);return 0;
        case stop_message:DestroyWindow(h);return 0;
        case WM_DESTROY:hwnd=nullptr;PostQuitMessage(0);return 0;
        case WM_CLOSE:command(6);return 0;
        case WM_PAINT:paint(h);return 0;
        case WM_TIMER:if(wp==1){KillTimer(h,1);InvalidateRect(h,nullptr,FALSE);return 0;}break;
        // Legacy window capturers use PrintWindow rather than DWM/Graphics Capture.
        case WM_PRINT:case WM_PRINTCLIENT:capture(reinterpret_cast<HDC>(wp));return 0;
        case WM_ERASEBKGND:return 1;
        case WM_SIZE:
            if(target && LOWORD(lp)&&HIWORD(lp))if(FAILED(target->Resize(D2D1::SizeU(LOWORD(lp),HIWORD(lp))))){bitmap.Reset();target.Reset();}
            InvalidateRect(h,nullptr,FALSE);return 0;
        case WM_SIZING: {
            int bw,bh;dimensions(bw,bh);*reinterpret_cast<RECT*>(lp)=locked_rect(*reinterpret_cast<RECT*>(lp),unsigned(wp),bw,bh,aspect);return TRUE;
        }
        case WM_DPICHANGED: {
            auto r=*reinterpret_cast<RECT*>(lp);int bw,bh;dimensions(bw,bh);r=locked_rect(r,WMSZ_BOTTOMRIGHT,bw,bh,aspect);
            SetWindowPos(h,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);return 0;
        }
        case WM_NCCALCSIZE:if(borderless&&wp)return 0;break;
        case WM_NCHITTEST:if(borderless) {
            RECT r{};GetWindowRect(h,&r);int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp),b=std::max(5,int(GetDpiForWindow(h)*6/96));
            bool l=x<r.left+b,rt=x>=r.right-b,t=y<r.top+b,bot=y>=r.bottom-b;
            return t?(l?HTTOPLEFT:rt?HTTOPRIGHT:HTTOP):bot?(l?HTBOTTOMLEFT:rt?HTBOTTOMRIGHT:HTBOTTOM):l?HTLEFT:rt?HTRIGHT:HTCLIENT;
        }break;
        case WM_LBUTTONDOWN:if(borderless){ReleaseCapture();SendMessageW(h,WM_NCLBUTTONDOWN,HTCAPTION,0);return 0;}break;
        case WM_KEYDOWN:
            if(wp=='B')command(1);else if(wp=='T')command(2);else if(wp=='F')command(3);
            else if(wp==VK_ESCAPE&&borderless)command(1);else break;return 0;
        case WM_CONTEXTMENU: {
            HMENU menu=CreatePopupMenu();
            AppendMenuW(menu,MF_STRING|(borderless?MF_CHECKED:0),1,L"Borderless capture window\tB");
            AppendMenuW(menu,MF_STRING|(topmost?MF_CHECKED:0),2,L"Always on top\tT");
            AppendMenuW(menu,MF_STRING,3,L"Fit to monitor\tF");AppendMenuW(menu,MF_STRING,4,L"50% size");AppendMenuW(menu,MF_STRING,5,L"100% size");
            AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,6,L"Hide preview");
            POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(p.x==-1&&p.y==-1)GetCursorPos(&p);
            auto id=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,h,nullptr);DestroyMenu(menu);command(id);return 0;
        }
        }return DefWindowProcW(h,msg,wp,lp);
    }
    void run(std::promise<bool> ready) noexcept {
        bool signalled=false,registered=false;
        auto old=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        try {
            if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf())))throw std::runtime_error("Direct2D unavailable");
            WNDCLASSEXW wc{sizeof(wc)};wc.lpfnWndProc=proc;wc.hInstance=module;wc.lpszClassName=window_class;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
            if(!RegisterClassExW(&wc))throw std::runtime_error("Preview window class unavailable");registered=true;
            if(!CreateWindowExW(WS_EX_APPWINDOW,window_class,window_title,decorated,CW_USEDEFAULT,CW_USEDEFAULT,960,580,nullptr,nullptr,module,this))throw std::runtime_error("Preview window creation failed");
            resize_client(960,540);ready.set_value(true);signalled=true;
            MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
        } catch(...) {if(!signalled)ready.set_value(false);}
        if(hwnd)DestroyWindow(hwnd);
        bitmap.Reset();target.Reset();factory.Reset();
        if(registered)UnregisterClassW(window_class,module);
        if(SUCCEEDED(com))CoUninitialize();
        if(old)SetThreadDpiAwarenessContext(old);
    }
};
Window::Window(HINSTANCE module):impl(std::make_unique<Impl>(module)) {
    std::promise<bool> ready;auto future=ready.get_future();
    impl->thread=std::thread([this,p=std::move(ready)]()mutable{impl->run(std::move(p));});
    if(!future.get()){impl->thread.join();throw std::runtime_error("Could not start SBS Preview window");}
}
Window::~Window(){if(impl->hwnd)PostMessageW(impl->hwnd,stop_message,0,0);if(impl->thread.joinable())impl->thread.join();}
void Window::submit(std::shared_ptr<Frame> frame) {
    std::lock_guard lock(impl->mutex);impl->pending=std::move(frame);
    if(!impl->posted)impl->posted=PostMessageW(impl->hwnd,frame_message,0,0)!=0;
}
void Window::show(){PostMessageW(impl->hwnd,show_message,0,0);}
HWND Window::handle()const{return impl->hwnd;}
}
