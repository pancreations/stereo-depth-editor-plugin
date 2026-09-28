#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace s3d::gpu {
// Own each allocation immediately. An interrupted resize leaves an empty pool,
// never an old shape pointing at a partially allocated/freed buffer set.
template<std::size_t N> struct BufferPool {
    std::array<std::uint64_t,N> pointers{};
    std::array<std::size_t,N> sizes{};
    int width{},height{};
    template<class Free> void clear(Free free) noexcept {
        width=height=0;sizes={};
        for(auto& pointer:pointers)if(auto owned=std::exchange(pointer,0))free(owned);
    }
    template<class Allocate,class Free>
    void resize(int w,int h,const std::array<std::size_t,N>& wanted,Allocate allocate,Free free) {
        if(width==w&&height==h&&sizes==wanted)return;
        clear(free);
        try {
            for(std::size_t n=0;n<N;++n)if(wanted[n])pointers[n]=allocate(wanted[n]);
            sizes=wanted;width=w;height=h;
        }catch(...){clear(free);throw;}
    }
};
}
