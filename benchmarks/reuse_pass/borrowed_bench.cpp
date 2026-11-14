#include "tests/reuse_pass/fixtures.h"
#include "experiments/borrowed_decode.h"
#include "allocation_counter.h"
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#if defined(__GNUC__) || defined(__clang__)
#define DO_NOT_INLINE __attribute__((noinline))
#else
#define DO_NOT_INLINE __declspec(noinline)
#endif
namespace r=portal::rpc;
DO_NOT_INLINE std::uint64_t own(const r::bytes& frame,r::detail::request& scratch,std::size_t i) {
    scratch.args.clear();
    if(!r::detail::decode_request_scratch(frame,scratch)||scratch.args.size()!=1)std::abort();
    auto text=scratch.args[0].take_string();
    return text.size()+(text.empty()?0:static_cast<unsigned char>(text[i%text.size()]));
}
DO_NOT_INLINE std::uint64_t borrowed(const r::bytes& frame,reuse_experiment::request_view& scratch,
                                    std::size_t i,bool materialize) {
    if(!reuse_experiment::decode(frame,scratch)||scratch.args.size()!=1)std::abort();
    const auto& v=scratch.args[0];
    if(materialize){
        const std::string text(v.data,v.size);
        return text.size()+(text.empty()?0:static_cast<unsigned char>(text[i%text.size()]));
    }
    return v.size+(v.size?static_cast<unsigned char>(v.data[i%v.size]):0);
}
int main(int argc,char** argv){
    if(argc!=4)return 2;
    const std::string mode=argv[1];const std::size_t size=static_cast<std::size_t>(std::stoull(argv[2])),n=static_cast<std::size_t>(std::stoull(argv[3]));
    if(!n)return 2;
    std::thread([]{}).join();
    const std::string text(size,'x');
    const auto frame=r::detail::encode_request_values(1,r::value::borrowed_string(text));
    r::detail::request owning;reuse_experiment::request_view views;
    auto once=[&](std::size_t i){return mode=="owning"?own(frame,owning,i):
        borrowed(frame,views,i,mode=="borrow-owning");};
    for(unsigned i=0;i<10000;++i)once(i);
    std::uint64_t sum=0;
#ifdef PORTAL_COUNT_ALLOCATIONS
    allocations=0;count_enabled=true;
#endif
    auto start=std::chrono::steady_clock::now();
    for(std::size_t i=0;i<n;++i)sum+=once(i);
    auto end=std::chrono::steady_clock::now();
#ifdef PORTAL_COUNT_ALLOCATIONS
    count_enabled=false;auto allocs=allocations;
#else
    const std::size_t allocs=0;
#endif
    if(sum!=n*(size+(size?120:0)))std::abort();
    std::printf("%s,%zu,%zu,%.6f,%llu,%zu\n",mode.c_str(),size,n,
        std::chrono::duration<double,std::nano>(end-start).count()/n,
        static_cast<unsigned long long>(sum),allocs);
}
