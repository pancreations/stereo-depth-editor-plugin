#pragma once
#include <windows.h>
#include <cstdint>
#include <memory>
#include <vector>
#include <array>

namespace s3d::preview {
inline constexpr wchar_t window_class[] = L"StereoDepth.SBSPreview.v1";
inline constexpr wchar_t window_title[] = L"StereoDepth SBS Preview";
inline constexpr UINT show_message = WM_APP + 3;
struct Frame {
    int width{}, height{};
    double aspect{16.0 / 9.0};
    std::vector<std::uint8_t> bgra; // top-down, tightly packed, display-referred SDR
};
// Adobe uncompressed PPix buffers have a lower-left origin; signed pitch is supported.
std::shared_ptr<Frame> copy_frame(const char* pixels, int width, int height,
                                std::ptrdiff_t pitch, unsigned par_num, unsigned par_den);
class FramePool {
public:
    std::shared_ptr<Frame> copy(const char* pixels,int width,int height,std::ptrdiff_t pitch,unsigned pn,unsigned pd);
    std::size_t allocated_bytes() const;
private:
    std::array<std::shared_ptr<Frame>,3> frames;
};
struct Extent { int width, height; };
Extent fit_extent(int width, int height, double aspect);
RECT locked_rect(RECT proposed, unsigned edge, int border_width, int border_height, double aspect);

class Window {
public:
    explicit Window(HINSTANCE module);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    void submit(std::shared_ptr<Frame> frame);
    void show();
    HWND handle() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
