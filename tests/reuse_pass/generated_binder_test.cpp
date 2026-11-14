// Full-checkout integration gate. Deliberately excluded in isolated-wire mode;
// the extraction harness cannot stand in for generated-binder coverage.
#include "portal/portal.h"
#include "portal/backends/rpc_reuse.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"CHECK: %s line %d\n",#c,__LINE__);std::abort();}}while(false)
namespace r=portal::rpc;
using V=r::value;
struct overloaded {
    int operator()(int n) const noexcept { return n+1; }
    int operator()(const std::string& text) const noexcept { return static_cast<int>(text.size()); }
};
int main(){
    portal::rpc_backend::export_context_t context;
    r::router server;
    server.define("zero",portal::create_export_callable_binder<int()>(context,[]() noexcept {return 42;}));
    server.define("echo",portal::create_export_callable_binder<std::string(std::string)>(context,
        [](std::string text) noexcept {return text;}));
    using overloads=portal::type_list<portal::free_callable<int(int)>,portal::free_callable<int(const std::string&)>>;
    server.define("overloaded",portal::create_export_overload_binder<overloads>(context,overloaded{}));
    int calls=0;
    server.define("checked",portal::create_export_callable_binder<int(std::string,int)>(context,
        [&calls](std::string text,int n) noexcept {++calls;return static_cast<int>(text.size())+n;}));
    r::call_workspace workspace(128*1024);
    r::reusing_loopback_transport transport(server);
    CHECK(r::round_trip_reusing(transport,workspace,r::detail::symbol_id("zero")).as_signed()==42);
    const std::string payload(65536,'k');
    auto held=r::round_trip_reusing(transport,workspace,r::detail::symbol_id("echo"),V::borrowed_string(payload));
    CHECK(held.copy_string()==payload);
    CHECK(r::round_trip_reusing(transport,workspace,r::detail::symbol_id("overloaded"),V::signed_integer(7)).as_signed()==8);
    CHECK(r::round_trip_reusing(transport,workspace,r::detail::symbol_id("overloaded"),V::borrowed_string(payload)).as_signed()==65536);
    r::dispatch_workspace dispatch;
    r::bytes output;
    for(auto bad : {V::boolean(true),V::signed_integer(std::numeric_limits<std::int64_t>::max())}){
        const auto frame=r::detail::encode_request_values(r::detail::symbol_id("checked"),V::borrowed_string(payload),bad);
        server.dispatch_into(frame,output,dispatch);
        CHECK(output==server.dispatch(frame));
        V ignored;std::string error;
        CHECK(r::detail::try_decode_response(output,ignored,error)==r::detail::response_status::remote_error);
        CHECK(calls==0); // whole-candidate probing still precedes side effects
    }
    CHECK(r::round_trip_reusing(transport,workspace,r::detail::symbol_id("checked"),V::borrowed_string(payload),V::signed_integer(2)).as_signed()==65538);
    CHECK(calls==1&&held.copy_string()==payload);
    std::puts("PASS: full generated binder, overloads, owning conversion and reject-before-side-effects");
}
