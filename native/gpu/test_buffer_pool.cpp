#include "buffer_pool.hpp"
#include <set>
#include <stdexcept>
#include <iostream>
int main(){
    using s3d::gpu::BufferPool;
    try {
        BufferPool<4> pool;std::set<std::uint64_t> live;std::uint64_t next=1;int calls=0,fail=-1;
        auto allocate=[&](size_t){if(++calls==fail)throw std::bad_alloc();live.insert(next);return next++;};
        auto release=[&](std::uint64_t p)noexcept{if(live.erase(p)!=1)std::terminate();};
        pool.resize(32,16,{64,64,64,64},allocate,release);
        for(int point=1;point<=4;++point) {
            calls=0;fail=point;
            try{pool.resize(64,32,{128,128,128,128},allocate,release);throw std::runtime_error("Failure injection did not fire");}catch(const std::bad_alloc&){}
            if(pool.width||pool.height||!live.empty())throw std::runtime_error("Failed resize retained stale dimensions or leaked allocations");
            for(auto pointer:pool.pointers)if(pointer)throw std::runtime_error("Failed resize retained pointer");
            calls=0;fail=-1;
            pool.resize(32,16,{64,64,64,64},allocate,release);
            if(calls!=4||live.size()!=4)throw std::runtime_error("Old resolution reused incomplete buffers after OOM");
        }
        calls=0;pool.resize(32,16,{64,64,64,64},allocate,release);
        if(calls)throw std::runtime_error("Unchanged buffers reallocated");
        pool.resize(32,16,{0,64,0,64},allocate,release);
        if(live.size()!=2||pool.pointers[0]||pool.pointers[2])throw std::runtime_error("Unused preview allocations retained");
        pool.clear(release);if(!live.empty())throw std::runtime_error("Shutdown leak");
        std::cout<<"GPU buffer failure injection at every allocation, recovery, reuse and release passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
