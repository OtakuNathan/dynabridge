#define PORTAL_POOL_COUNT
#include "portal/async.h"
#include "benchmarks/reuse_pass/pool_fixture.h"
#include <atomic>
#include <cstdio>
#include <functional>
#include <thread>
#include <vector>
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"CHECK: %s line %d\n",#c,__LINE__);std::abort();}}while(false)

void final_token_and_resource() {
    std::function<void()> finish;
    std::weak_ptr<reuse_test::pool_resource> weak;
    bool delivered=false;
    {
        auto resource=std::make_shared<reuse_test::pool_resource>();weak=resource;
        auto call=portal::make_invocation<int>(std::make_shared<int>(3),
            [&](int& x,auto done) noexcept {
                finish=[done,x]{done.finish(portal::async_result<int>(x));};
                return portal::async_submission::accept();
            });
        std::move(call).start(std::allocator_arg,reuse_test::pool_allocator<char>(resource),
            [&](portal::async_result<int> r) noexcept {
                CHECK(r.has_value()&&*r.value==3);finish={};delivered=true;
                CHECK(!weak.expired());
            });
    }
    CHECK(!weak.expired());
    std::thread worker([&]{finish();});worker.join();
    CHECK(delivered&&weak.expired());
}

void delayed_tokens() {
    auto resource=std::make_shared<reuse_test::pool_resource>(8);
    reuse_test::pool_allocator<char> a(resource);
    std::function<void()> finish,detach;
    int delivered=0;
    auto owner=std::make_shared<int>(7);std::weak_ptr<int> context=owner;
    auto call=portal::make_invocation<int>(std::move(owner),
        [&](int&,auto done) noexcept {
            finish=[done]{done.finish(portal::async_result<int>(7));};
            detach=[done]{done.detach(std::make_error_code(std::errc::timed_out));};
            return portal::async_submission::accept();
        });
    std::move(call).start(std::allocator_arg,a,[&](portal::async_result<int> r) noexcept {
        CHECK(!r.has_value());++delivered;
    });
    detach();CHECK(delivered==1 && !context.expired() && resource->outstanding.load()==1);
    finish();CHECK(delivered==1 && context.expired() && resource->outstanding.load()==1);
    auto duplicate=finish;finish={};detach={};
    CHECK(resource->outstanding.load()==1);
    duplicate();duplicate={};CHECK(resource->outstanding.load()==0);
}

void concurrent_pool() {
    auto resource=std::make_shared<reuse_test::pool_resource>(32);
    reuse_test::pool_allocator<char> a(resource);
    std::atomic<int> count{0};std::vector<std::thread> threads;
    for(int i=0;i<4;++i)threads.emplace_back([&]{
        auto context=std::make_shared<int>(1);
        for(int n=0;n<5000;++n) {
            auto call=portal::make_invocation<int>(context,[](int& x,auto done) noexcept {
                done.finish(portal::async_result<int>(x));return portal::async_submission::accept();
            });
            std::move(call).start(std::allocator_arg,a,[&](portal::async_result<int> r) noexcept {
                CHECK(r.has_value()&&*r.value==1);++count;
            });
        }
    });
    for(auto& t:threads)t.join();
    CHECK(count.load()==20000 && resource->outstanding.load()==0);
}

void rejection_and_failure() {
    auto resource=std::make_shared<reuse_test::pool_resource>();
    reuse_test::pool_allocator<char> a(resource);
    int completed=0;
    auto call=portal::make_invocation<void>(std::make_shared<int>(0),[](int&,auto) noexcept {
        return portal::async_submission::reject(std::make_error_code(std::errc::invalid_argument));
    });
    auto handler=[&](portal::async_result<void> r) noexcept {CHECK(!r.has_value());++completed;};
#if PORTAL_HAS_EXCEPTIONS
    resource->fail_next=true;bool failed=false;
    try {std::move(call).start(std::allocator_arg,a,handler);}
    catch(const std::bad_alloc&){failed=true;}
    CHECK(failed && completed==0 && resource->outstanding.load()==0);
#endif
    std::move(call).start(std::allocator_arg,a,handler);
    CHECK(completed==1 && resource->outstanding.load()==0);
}
int main(){final_token_and_resource();delayed_tokens();concurrent_pool();rejection_and_failure();
    std::puts("PASS: allocator lifetime, callback self-destruction, delayed tokens, cross-thread release, concurrent pool, reject/allocation failure");}
