#include "tests/reuse_pass/fixtures.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <thread>

#include "allocation_counter.h"

namespace r=portal::rpc;
using reuse_test::value;
struct ordinary_call {
    r::router& server;
    template<class... V> value operator()(std::uint64_t method,const V&... args) {
        auto request=r::detail::encode_request_values(method,args...);
        auto response=server.dispatch(request);
        return r::detail::decode_response(response);
    }
};
#ifndef PORTAL_REUSE_BASELINE
struct reused_call {
    r::call_workspace buffers{1024*1024};
    r::reusing_loopback_transport transport;
    explicit reused_call(r::router& s):transport(s){}
    template<class... V> value operator()(std::uint64_t method,const V&... args) {
        return r::round_trip_reusing(transport,buffers,method,args...);
    }
};
#endif

template<class Call> void run(Call& call,const std::string& mode,int length,std::size_t n) {
    const bool scalar=length<0;
    const auto id=r::detail::symbol_id(scalar?"add":"echo");
    const std::string payload(scalar?0:static_cast<std::size_t>(length),'x');
    std::uint64_t sum=0;
    auto once=[&](std::size_t i){
        if(scalar) {
            auto result=call(id,value::signed_integer(i%1024),value::signed_integer(3));
            sum+=result.as_signed();
        }else{
            auto result=call(id,value::borrowed_string(payload));
            if(result.string_size()!=payload.size())std::abort();
            sum+=result.string_size();
            if(length)sum+=static_cast<unsigned char>(result.string_data()[i%length]);
        }
    };
    for(std::size_t i=0;i<10000;++i)once(i);
    sum=0;
#ifdef PORTAL_COUNT_ALLOCATIONS
    allocations=0;count_enabled=true;
#endif
    const auto start=std::chrono::steady_clock::now();
    for(std::size_t i=0;i<n;++i)once(i);
    const auto end=std::chrono::steady_clock::now();
#ifdef PORTAL_COUNT_ALLOCATIONS
    count_enabled=false;
    const auto allocs=allocations;
#else
    const std::size_t allocs=0;
#endif
    std::uint64_t expected=0;
    for(std::size_t i=0;i<n;++i)
        expected+=scalar?(i%1024+3):(payload.size()+(length?120:0));
    if(sum!=expected)std::abort();
    const double ns=std::chrono::duration<double,std::nano>(end-start).count()/n;
    std::printf("%s,%d,%zu,%.6f,%llu,%zu\n",mode.c_str(),length,n,ns,
        static_cast<unsigned long long>(sum),allocs);
}
int main(int argc,char** argv){
    if(argc!=4){std::fprintf(stderr,"usage: rpc_bench ordinary|reuse length(-1=scalar) iterations\n");return 2;}
    const std::string mode=argv[1];const int length=std::stoi(argv[2]);
    const auto n=std::stoull(argv[3]);if(length< -1||!n)return 2;
    // Make the process genuinely multithreaded before timing, identically in all modes.
    std::thread([]{}).join();
    r::router server;server.define("add",reuse_test::add{});server.define("echo",reuse_test::echo{});
    if(mode=="ordinary"){ordinary_call call{server};run(call,mode,length,n);}
#ifndef PORTAL_REUSE_BASELINE
    else if(mode=="reuse"){reused_call call(server);run(call,mode,length,n);}
#endif
    else return 2;
}
