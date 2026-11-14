#include "portal/async.h"
#include "pool_fixture.h"
#include "allocation_counter.h"
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

struct complete_inline {
    template<class Token> portal::async_submission operator()(int& x,Token done) const noexcept {
        done.finish(portal::async_result<int>(x));return portal::async_submission::accept();
    }
};
template<bool Pool> void bench(std::size_t n) {
    std::thread([]{}).join();
    auto owner=std::make_shared<int>(17);
    auto resource=std::make_shared<reuse_test::pool_resource>();
    reuse_test::pool_allocator<unsigned char> allocator(resource);
    std::uint64_t sum=0;
    auto once=[&](){
        auto call=portal::make_invocation<int>(owner,complete_inline{});
        auto handler=[&](portal::async_result<int> r) noexcept {
            if(!r.has_value())std::abort();
            sum+=*r.value;
        };
#ifndef PORTAL_REUSE_BASELINE
        if(Pool)std::move(call).start(std::allocator_arg,allocator,std::move(handler));
        else
#endif
            std::move(call).start(std::move(handler));
    };
    for(unsigned i=0;i<10000;++i)once();
    sum=0;
    #ifdef PORTAL_COUNT_ALLOCATIONS
    allocations=0;count_enabled=true;
#endif
    const auto start=std::chrono::steady_clock::now();
    for(std::size_t i=0;i<n;++i)once();
    const auto end=std::chrono::steady_clock::now();
#ifdef PORTAL_COUNT_ALLOCATIONS
    count_enabled=false;
    const auto allocs=allocations;
#else
    const std::size_t allocs=0;
#endif
    if(sum!=17*n||resource->outstanding.load()!=0)std::abort();
    std::printf("%s,%zu,%.6f,%llu,%zu\n",Pool?"pool":"system",n,
        std::chrono::duration<double,std::nano>(end-start).count()/n,
        static_cast<unsigned long long>(sum),allocs);
}
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    const auto n=std::stoull(argv[2]);if(!n)return 2;
    if(std::string(argv[1])=="system")bench<false>(n);
#ifndef PORTAL_REUSE_BASELINE
    else if(std::string(argv[1])=="pool")bench<true>(n);
#endif
    else return 2;
}
