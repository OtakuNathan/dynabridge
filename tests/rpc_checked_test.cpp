#include "portal/portal.h"
#include "portal/backends/rpc.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::abort(); } } while(false)
int main() {
    namespace codec = portal::rpc::detail;
    using value = portal::rpc::value;
    using status = codec::response_status;
    portal::rpc::router router;
    portal::rpc_backend::export_context_t context;
    int calls = 0;
    router.define("increment", portal::create_export_callable_binder<int(int)>(context,
        [&](int n) noexcept { ++calls; return n + 1; }));
    router.define("notify", portal::create_export_callable_binder<void(int)>(context,
        [&](int) noexcept { ++calls; }));
    router.define("echo", portal::create_export_callable_binder<std::string(std::string)>(context,
        [](std::string text) noexcept { return text; }));
    value output; std::string error;
    for (auto name : {"increment", "notify"}) {
        for (const auto& args : std::vector<std::vector<value>>{{}, {value::boolean(true)},
                {value::signed_integer(1),value::signed_integer(2)}}) {
            CHECK(codec::try_decode_response(router.dispatch(codec::encode_request(codec::symbol_id(name),args)),output,error)==status::remote_error);
        }
    }
    CHECK(calls==0);
    for (auto name : {"increment", "notify"}) {
        CHECK(codec::try_decode_response(router.dispatch(codec::encode_request(codec::symbol_id(name),{value::signed_integer(7)})),output,error)==status::ok);
    }
    CHECK(calls==2);
    CHECK(codec::try_decode_response(router.dispatch(codec::encode_request(codec::symbol_id("echo"),{value::string("hello")})),output,error)==status::ok);
    auto text=portal::from_optional<std::string>(context,output);
    CHECK(text && *text=="hello");
    auto request=codec::encode_request(codec::symbol_id("increment"),{value::signed_integer(7)});
    for(std::size_t size=0;size<request.size();++size) {
        codec::request parsed; parsed.method=99;
        CHECK(!codec::try_decode_request(portal::rpc::bytes(request.begin(),request.begin()+size),parsed));
        CHECK(parsed.method==99);
    }
    auto malformed=request; malformed[17]=255; codec::request parsed;
    CHECK(!codec::try_decode_request(malformed,parsed));
    malformed=request; malformed.push_back(0); CHECK(!codec::try_decode_request(malformed,parsed));
    auto response=codec::encode_error("failure");
    CHECK(codec::try_decode_response(response,output,error)==status::remote_error && error=="failure");
    response.push_back(0); CHECK(codec::try_decode_response(response,output,error)==status::malformed);
    response=codec::encode_response(value::signed_integer(42));
    for(std::size_t size=0;size<response.size();++size) {
        output=value::signed_integer(99);
        CHECK(codec::try_decode_response(portal::rpc::bytes(response.begin(),response.begin()+size),output,error)==status::malformed);
        CHECK(output.as_signed()==99);
    }
#if PORTAL_HAS_EXCEPTIONS
    router.define("runtime_failure",portal::create_export_callable_binder<int()>(context,
        []() -> int { throw std::runtime_error("runtime failure, not a conversion mismatch"); }));
    CHECK(codec::try_decode_response(router.dispatch(codec::encode_request(codec::symbol_id("runtime_failure"),{})),output,error)==status::remote_error);
    CHECK(error=="runtime failure, not a conversion mismatch");
#endif
}
