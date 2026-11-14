#include "fixture.h"
#include "portal/rpc/typed_async.h"
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>

struct transport {
    tr::router server;
    bool deferred=false, reject=false, corrupt=false, destroyed=false;
    w::bytes pending_request;
    std::function<void(w::bytes,portal::async_error)> pending;
    transport(){bind(server);}
    template<class H> portal::async_submission async_round_trip(w::bytes request,H done) PORTAL_NOEXCEPT_IF_DISABLED {
        if(reject)return portal::async_submission::reject(std::make_error_code(std::errc::connection_refused));
        if(deferred){pending_request=std::move(request);pending=std::move(done);}
        else {w::bytes response;server.dispatch_into(request,response);if(corrupt)response[0]^=1;done(std::move(response),{});}
        return portal::async_submission::accept();
    }
    void complete(){w::bytes response;server.dispatch_into(pending_request,response);pending_request.clear();pending(std::move(response),{});}
};
// Strict no-exception contract: submission itself must declare noexcept.
struct no_throw_transport {
    tr::router server;
    no_throw_transport(){bind(server);}
    template<class H> portal::async_submission async_round_trip(w::bytes request,H done) noexcept {
        w::bytes response;server.dispatch_into(request,response);done(std::move(response),{});
        return portal::async_submission::accept();
    }
};
struct racing_transport {
    tr::router server;std::thread worker;
    racing_transport(){bind(server);}
    template<class H> portal::async_submission async_round_trip(w::bytes request,H done) PORTAL_NOEXCEPT_IF_DISABLED {
        worker=std::thread([this,request=std::move(request),done=std::move(done)]() mutable {
            w::bytes response;server.dispatch_into(request,response);done(std::move(response),{});
        });
        std::this_thread::yield();
        return portal::async_submission::accept();
    }
};
#if PORTAL_HAS_EXCEPTIONS
struct throwing_transport {
    template<class H> portal::async_submission async_round_trip(w::bytes,H) {
        throw std::runtime_error("submit failed before acceptance");
    }
};
#endif
int main(){
    auto safe=std::make_shared<no_throw_transport>();int simple=0;
    auto first=tr::make_rpc_invocation<Add>(safe,{},std::int32_t(20),std::int32_t(22));
    std::move(first).start([&](portal::async_result<std::int32_t> r) noexcept {CHECK(r.has_value());CHECK(*r.value==42);++simple;});
    CHECK(simple==1);
    auto t=std::make_shared<transport>();int calls=0;
    auto call=tr::make_rpc_invocation<Echo>(t,{},std::string(1000,'a'));
    std::move(call).start([&](portal::async_result<std::string> r) noexcept {CHECK(r.has_value());CHECK(r.value->size()==1000);++calls;});
    CHECK(calls==1);
    t->deferred=true;
    std::string source(1000,'b');
    auto next=tr::make_rpc_invocation<Echo>(t,{},source);source.assign(1000,'c');
    std::move(next).start([&](portal::async_result<std::string> r) noexcept {
        CHECK(r.has_value());CHECK(*r.value==std::string(1000,'b'));++calls;
        t->pending={}; // destroys the callback that owns the final completion token
    });
    CHECK(calls==1);t->complete();CHECK(calls==2);
    t->reject=true;
    auto rejected=tr::make_rpc_invocation<Add>(t,{},std::int32_t(1),std::int32_t(2));
    std::move(rejected).start([&](portal::async_result<std::int32_t> r) noexcept {CHECK(!r.has_value());CHECK(r.error.code==std::errc::connection_refused);++calls;});
    CHECK(calls==3);CHECK(!t->pending);
    t->reject=false;t->deferred=false;t->corrupt=true;
    auto bad=tr::make_rpc_invocation<Add>(t,{},std::int32_t(1),std::int32_t(2));
    std::move(bad).start([&](portal::async_result<std::int32_t> r) noexcept {CHECK(!r.has_value());CHECK(r.error.code==tr::make_error_code(w::errc::malformed));++calls;});
    CHECK(calls==4);
    t->corrupt=false;t->deferred=true;
    auto threaded=tr::make_rpc_invocation<EchoPacket>(t,{},packet());
    std::move(threaded).start([&](portal::async_result<Packet> r) noexcept {CHECK(r.has_value());CHECK(same(*r.value,packet()));++calls;t->pending={};});
    std::thread completion([&]{t->complete();});completion.join();CHECK(calls==5);
    // Transport lifetime survives release of the external owner.
    std::weak_ptr<transport> weak=t;
    auto last=tr::make_rpc_invocation<Add>(t,{},std::int32_t(19),std::int32_t(23));
    std::move(last).start([&](portal::async_result<std::int32_t> r) noexcept {CHECK(r.has_value());CHECK(*r.value==42);++calls;});
    transport* raw=t.get();t.reset();CHECK(!weak.expired());
    auto keeper=weak.lock();raw->complete();keeper->pending={};keeper.reset();CHECK(weak.expired());CHECK(calls==6);
    auto race=std::make_shared<racing_transport>();int finishes=0;
    for(std::int32_t i=0;i<200;++i){
        auto operation=tr::make_rpc_invocation<Add>(race,{},i,std::int32_t(1));
        std::move(operation).start([&,i](portal::async_result<std::int32_t> r) noexcept {CHECK(r.has_value());CHECK(*r.value==i+1);++finishes;});
        race->worker.join();
    }
    CHECK(finishes==200);
#if PORTAL_HAS_EXCEPTIONS
    auto throwing=std::make_shared<throwing_transport>();int rejected_count=0;
    auto failed=tr::make_rpc_invocation<Add>(throwing,{},std::int32_t(1),std::int32_t(2));
    std::move(failed).start([&](portal::async_result<std::int32_t> r) noexcept {CHECK(!r.has_value());CHECK(r.error.exception);++rejected_count;});
    CHECK(rejected_count==1);
#endif
    std::puts("typed RPC async: passed");
}
