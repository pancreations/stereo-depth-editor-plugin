#include "preview_window.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace s3d::preview {
Extent fit_extent(int width, int height, double aspect) {
    if (width < 1 || height < 1 || !std::isfinite(aspect) || aspect <= 0)
        throw std::invalid_argument("Invalid preview geometry");
    if (double(width) / height > aspect) width = std::max(1, int(std::lround(height * aspect)));
    else height = std::max(1, int(std::lround(width / aspect)));
    return {width, height};
}
RECT locked_rect(RECT r, unsigned edge, int bw, int bh, double aspect) {
    int w = std::max(64L, r.right - r.left - bw);
    int h = std::max(32L, r.bottom - r.top - bh);
    if (edge == WMSZ_TOP || edge == WMSZ_BOTTOM) w = std::max(1, int(std::lround(h * aspect)));
    else h = std::max(1, int(std::lround(w / aspect)));
    if (edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT || edge == WMSZ_BOTTOMLEFT) r.left = r.right - w - bw;
    else r.right = r.left + w + bw;
    if (edge == WMSZ_TOP || edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT) r.top = r.bottom - h - bh;
    else r.bottom = r.top + h + bh;
    return r;
}
void fill_frame(Frame& frame,const char* pixels, int w, int h, std::ptrdiff_t pitch, unsigned pn, unsigned pd) {
    if (!pixels || w < 1 || h < 1 || w > 32768 || h > 32768 || std::uint64_t(w)*h > 67108864 ||
        pitch == PTRDIFF_MIN || std::abs(pitch) < std::ptrdiff_t(w)*4 || !pn || !pd)
        throw std::invalid_argument("Invalid preview frame");
    auto* f=&frame;f->width=w; f->height=h;
    f->aspect=double(w)*pn/(double(h)*pd);
    const auto bytes=size_t(w)*h*4;
    if(f->bgra.size()!=bytes)std::vector<std::uint8_t>(bytes).swap(f->bgra);
    for (int y=0; y<h; ++y)
        std::memcpy(f->bgra.data()+size_t(y)*w*4, pixels+std::ptrdiff_t(h-1-y)*pitch, size_t(w)*4);
}
std::shared_ptr<Frame> copy_frame(const char* pixels,int w,int h,std::ptrdiff_t pitch,unsigned pn,unsigned pd) {
    auto frame=std::make_shared<Frame>();fill_frame(*frame,pixels,w,h,pitch,pn,pd);return frame;
}
std::shared_ptr<Frame> FramePool::copy(const char* pixels,int w,int h,std::ptrdiff_t pitch,unsigned pn,unsigned pd) {
    for(auto& frame:frames)if(!frame||frame.use_count()==1) {
        if(!frame)frame=std::make_shared<Frame>();
        fill_frame(*frame,pixels,w,h,pitch,pn,pd);return frame;
    }
    return {}; // All three buffers are displayed/queued/in use: drop, never grow.
}
std::size_t FramePool::allocated_bytes()const {
    size_t bytes=0;for(const auto& frame:frames)if(frame)bytes+=frame->bgra.capacity();return bytes;
}
}
