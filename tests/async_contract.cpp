#include <dynabridge/async.h>
#include <atomic>
#include <cstdio>
#include <thread>
using namespace dynabridge;
#define CHECK(x) do {if(!(x)) {std::fprintf(stderr,"failed line %d: %s\n",__LINE__,#x);std::abort();}} while(false)
struct move_result {int n;explicit move_result(int x) noexcept:n(x){} move_result(move_result&&) noexcept=default;move_result(const move_result&)=delete;};
struct context {int n=41;};
int main(){
    auto owner=std::make_shared<context>();
    int count=0;
    auto immediate=[](context& c,auto token) noexcept {token.finish(async_result<move_result>(move_result(c.n+1)));return async_submission::accept();};
    {
        auto call=make_invocation<move_result>(owner,immediate);
        std::move(call).start([&](async_result<move_result> r) noexcept {CHECK(r.value->n==42);++count;});
    }
    {
        auto call=erase_invocation(make_invocation<move_result>(owner,immediate));
        std::move(call).start([&](async_result<move_result> r) noexcept {CHECK(r.value->n==42);++count;});
    }
    CHECK(count==2);
    {
        auto call=make_invocation<int>(owner,[](context&,auto) noexcept {return async_submission::reject(std::make_error_code(std::errc::device_or_resource_busy));});
        std::move(call).start([&](async_result<int> r) noexcept {CHECK(!r.has_value());CHECK(r.error.code==std::errc::device_or_resource_busy);++count;});
    }
    std::function<void()> finish,detach;
    auto borrow=std::make_shared<int>(7);std::weak_ptr<int> weak=borrow;
    auto call=make_invocation<int>(owner,[&](context&,auto token) noexcept {
        finish=[token]{token.finish(async_result<int>(9));};
        detach=[token]{token.detach(std::make_error_code(std::errc::timed_out));};
        return async_submission::accept();
    },std::move(borrow));
    std::move(call).start([&](async_result<int> r) noexcept {CHECK(!r.has_value());CHECK(!weak.expired());++count;});
    detach(); CHECK(count==4 && !weak.expired());
    std::thread worker([&]{finish();});worker.join();
    CHECK(weak.expired() && count==4);
    finish();detach();CHECK(count==4);
    {
        auto access=std::make_shared<int>(1);std::weak_ptr<int> guard=access;
        auto nested=make_invocation<int>(owner,[](context&,auto token) noexcept {token.finish(async_result<int>(1));return async_submission::accept();},std::move(access));
        std::move(nested).start([&](async_result<int>) noexcept {CHECK(guard.expired());++count;});
    }
    {
        auto unused_owner=std::make_shared<context>();std::weak_ptr<context> lifetime=unused_owner;
        {auto unused=make_invocation<int>(std::move(unused_owner),[](context&,auto) noexcept {std::abort();return async_submission::accept();});}
        CHECK(lifetime.expired());
    }
    for(int i=0;i<100;++i){
        std::atomic<int> delivered{0};std::function<void()> complete,timeout;
        auto concurrent=make_invocation<void>(owner,[&](context&,auto token) noexcept {
            complete=[token]{token.finish(async_result<void>());};
            timeout=[token]{token.detach(std::make_error_code(std::errc::timed_out));};return async_submission::accept();
        });
        std::move(concurrent).start([&](async_result<void>) noexcept {++delivered;});
        std::thread a(complete),b(timeout);a.join();b.join();complete();CHECK(delivered==1);
    }
    {
        auto rejected=make_invocation<int>(owner,[](context&,auto token) noexcept {
            token.detach(std::make_error_code(std::errc::timed_out));
            return async_submission::reject(std::make_error_code(std::errc::connection_refused));
        });
        std::move(rejected).start([](async_result<int> r) noexcept {CHECK(r.error.code==std::errc::connection_refused);});
    }
    CHECK(count==5);
    std::puts("async contracts passed");
}
