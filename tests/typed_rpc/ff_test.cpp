// Full integration gate: requires REAL FF and Portal headers. No FF stub exists.
#include "fixture.h"
#include "portal/rpc/typed_async.h"
#include "portal/extensions/flux_foundry/async.h"
#include <deque>
#include <functional>

struct queued_transport {
    tr::router server;
    bool reject = false;
    std::deque<std::function<void()>> pending;
    queued_transport(){bind(server);}
    template<class H> portal::async_submission async_round_trip(w::bytes request,H handler) {
        if(reject)return portal::async_submission::reject(std::make_error_code(std::errc::connection_refused));
        pending.emplace_back([this,request=std::move(request),handler=std::move(handler)]() mutable {
            w::bytes response;server.dispatch_into(request,response);handler(std::move(response),{});
        });
        return portal::async_submission::accept();
    }
    void drain(){while(!pending.empty()){auto work=std::move(pending.front());pending.pop_front();work();}}
};
struct sink {
    using value_type=flux_foundry::result_t<std::int32_t,portal::async_error>;
    int* completed;std::int64_t* total;bool rejection;
    void emplace(value_type&& result) noexcept {
        if(rejection){CHECK(!result.has_value());CHECK(result.error().code==std::errc::connection_refused);}
        else{CHECK(result.has_value());*total+=result.value();}
        ++*completed;
    }
};
int main(){
    auto transport=std::make_shared<queued_transport>();
    using Call=decltype(tr::make_rpc_invocation<Add>(transport,w::limits{},std::int32_t(1),std::int32_t(2)));
    auto blueprint=flux_foundry::make_blueprint<Call,portal::async_error>()
        | portal::ff::await_static_invocation<Call>() | flux_foundry::end();
    int completed=0;std::int64_t total=0;
    for(std::int32_t i=0;i<1000;++i){
        auto runner=flux_foundry::make_fast_runner_view(blueprint,sink{&completed,&total,false});
        runner(tr::make_rpc_invocation<Add>(transport,{},i,std::int32_t(2)));
    }
    CHECK(completed==0);transport->drain();CHECK(completed==1000);CHECK(total==501500);
    transport->reject=true;
    auto rejected=flux_foundry::make_fast_runner_view(blueprint,sink{&completed,&total,true});
    rejected(tr::make_rpc_invocation<Add>(transport,{},std::int32_t(1),std::int32_t(2)));
    CHECK(completed==1001);CHECK(transport->pending.empty());
    std::puts("typed RPC + real FF reused blueprint: passed");
}
