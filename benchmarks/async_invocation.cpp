#include <dynabridge/extensions/flux_foundry/async.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
#include <cstdlib>
#include <new>
std::atomic<unsigned long long> allocations{0};
void* operator new(std::size_t n){allocations.fetch_add(1,std::memory_order_relaxed);if(auto p=std::malloc(n?n:1))return p;std::abort();}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
struct large_result {
    std::vector<int> values;
    explicit large_result(int n):values(16384,n){}
    large_result()=delete;large_result(const large_result&)=delete;
    large_result(large_result&&) noexcept=default;
    large_result& operator=(large_result&&) noexcept=default;
};
using Clock=std::chrono::steady_clock;
template<class Make,class Run> void measure(const char* mode,const char* scenario,int count,Make make,Run run){
    for(int i=0;i<100;++i)run(make());
    auto a=allocations.load();auto begin=Clock::now();
    for(int i=0;i<count;++i)run(make());
    auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-begin).count();
    std::printf("%s,%s,%d,%.2f,%.3f\n",mode,scenario,count,double(ns)/count,double(allocations.load()-a)/count);
}
template<bool Erased> struct wrap;
template<> struct wrap<false>{template<class C>static C get(C c){return c;}};
template<> struct wrap<true>{template<class C>static auto get(C c){return dynabridge::erase_invocation(std::move(c));}};
template<bool Erased> void bench(const char* name){
    auto ctx=std::make_shared<int>(41);
    auto make=[&]{return wrap<Erased>::get(dynabridge::make_invocation<int>(ctx,[](int& n,auto done) noexcept {done.finish(dynabridge::async_result<int>(n+1));return dynabridge::async_submission::accept();}));};
    std::atomic<unsigned> checksum{0};
    measure(name,"discard",20000,make,[](auto){});
    measure(name,"inline",20000,make,[&](auto call){std::move(call).start([&](dynabridge::async_result<int> r) noexcept {checksum+=*r.value;});});
    measure(name,"ff_inline",10000,make,[&](auto call){dynabridge::ff::start_invocation(std::move(call),[&](flux_foundry::result_t<int,dynabridge::async_error> r) noexcept {checksum+=r.value();});});
    auto large=[&]{return wrap<Erased>::get(dynabridge::make_invocation<large_result>(ctx,[](int& n,auto done) noexcept {done.finish(dynabridge::async_result<large_result>(large_result(n)));return dynabridge::async_submission::accept();}));};
    measure(name,"move_only_64k",2000,large,[&](auto call){std::move(call).start([&](dynabridge::async_result<large_result> r) noexcept {checksum+=r.value->values[0];});});
    // Thread creation is intentionally shared by both modes and included, not pure handoff latency.
    auto threaded=[&]{return wrap<Erased>::get(dynabridge::make_invocation<int>(ctx,[](int& n,auto done) noexcept {std::thread worker([n,done]{done.finish(dynabridge::async_result<int>(n));});worker.join();return dynabridge::async_submission::accept();}));};
    measure(name,"thread_create_join",500,threaded,[&](auto call){std::move(call).start([&](dynabridge::async_result<int> r) noexcept {checksum+=*r.value;});});
    if(!checksum.load())std::abort();
}
int main(){std::puts("mode,scenario,iterations,ns_per_call,allocations_per_call");bench<false>("static");bench<true>("erased");}
