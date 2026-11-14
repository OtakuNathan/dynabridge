#include "fixtures.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <thread>
#include <vector>

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr,"CHECK failed: %s (%d)\n",#c,__LINE__); std::abort(); } } while (false)
using namespace portal;
namespace r = portal::rpc;
using reuse_test::value;

struct retaining_binder {
    using signature_t = int(std::string);
    value* saved;
    reuse_test::result call_optional(const value& input) {
        *saved=input;
        return value::signed_integer(static_cast<std::int64_t>(input.string_size()));
    }
};


void parity() {
    r::bytes out;
    const std::uint64_t method = 123456789;
    const std::string text("ab\0\xff\n",5);
    std::vector<value> vals;
    vals.push_back(value());
    vals.push_back(value::signed_integer(std::numeric_limits<std::int64_t>::min()));
    vals.push_back(value::unsigned_integer(std::numeric_limits<std::uint64_t>::max()));
    vals.push_back(value::real(-0.0));
    vals.push_back(value::real(std::numeric_limits<double>::quiet_NaN()));
    vals.push_back(value::boolean(true));
    vals.push_back(value::boolean(false));
    vals.push_back(value::string(""));
    vals.push_back(value::string(text));
    vals.push_back(value::string(std::string(65536,'x')));
    for (const auto& v : vals) {
        r::detail::encode_request_values_into(out,method,v);
        CHECK(out==r::detail::encode_request_values(method,v));
        r::detail::encode_response_into(out,v);
        CHECK(out==r::detail::encode_response(v));
    }
    r::detail::encode_request_values_into(out,method);
    CHECK(out==r::detail::encode_request_values(method));
    for (const auto& message : {std::string(), text, std::string(1024,'e')}) {
        r::detail::encode_error_into(out,message);
        CHECK(out==r::detail::encode_error(message));
    }
    std::mt19937_64 rng(41);
    for (unsigned i=0;i<10000;++i) {
        auto a=value::unsigned_integer(rng()), b=value::unsigned_integer(rng());
        r::detail::encode_request_values_into(out,method,a,b);
        CHECK(out==r::detail::encode_request_values(method,a,b));
    }
}

void malformed_and_alias() {
    r::router server;
    int called=0;
    server.define("call",reuse_test::callback{[&](value& a)->reuse_test::result {
        ++called; return a;
    }});
    const auto method=r::detail::symbol_id("call");
    auto valid=r::detail::encode_request_values(method,value::signed_integer(5));
    r::dispatch_workspace ws;
    r::bytes output;
    std::vector<r::bytes> corpus;
    for (std::size_t n=0;n<valid.size();++n) corpus.emplace_back(valid.begin(),valid.begin()+n);
    for (std::size_t p=0;p<valid.size();++p) {
        auto frame=valid;frame[p]^=0xff;corpus.push_back(std::move(frame));
    }
    {auto frame=valid;frame.push_back(0);corpus.push_back(std::move(frame));}
    {auto frame=valid;for(unsigned p=13;p<17;++p)frame[p]=0xff;corpus.push_back(std::move(frame));}
    std::mt19937 rng(192);
    for(unsigned k=0;k<2000;++k) {
        r::bytes frame(rng()%80);for(auto& b:frame)b=static_cast<std::uint8_t>(rng());
        corpus.push_back(std::move(frame));
    }
    corpus.push_back(valid);
    for (const auto& frame:corpus) {
        int initial=called;
        auto reference=server.dispatch(frame);
        int effects=called-initial;
        initial=called;
        server.dispatch_into(frame,output,ws);
        CHECK(output==reference);CHECK(called-initial==effects);
        auto inplace=frame;
        server.dispatch_into(inplace,inplace,ws);
        CHECK(inplace==reference);
    }
    auto string_frame=r::detail::encode_request_values(method,value::string(std::string(65536,'s')));
    const auto string_expected=server.dispatch(string_frame);
    server.dispatch_into(string_frame,string_frame,ws);
    CHECK(string_frame==string_expected); // request/output alias also for owning strings
    r::detail::request preserved;
    preserved.method=999;preserved.args.push_back(value::signed_integer(88));
    CHECK(!r::detail::try_decode_request({},preserved));
    CHECK(preserved.method==999 && preserved.args[0].as_signed()==88);
    // Reuse after a partially decoded string followed by invalid trailing data.
    auto trailing=r::detail::encode_request_values(method,value::string(std::string(4096,'x')));
    trailing.push_back(33);server.dispatch_into(trailing,output,ws);
    server.dispatch_into(valid,output,ws);
    CHECK(r::detail::decode_response(output).as_signed()==5);
}

void recursive_and_ownership() {
    r::router server;
    r::call_workspace client(1024);
    r::reusing_loopback_transport transport(server,4);
    const auto recurse=r::detail::symbol_id("recurse");
    server.define("recurse",reuse_test::callback{[&](value& a)->reuse_test::result {
        const auto n=a.as_signed();
        if(n==0)return value::signed_integer(0);
        auto inner=r::round_trip_reusing(transport,client,recurse,value::signed_integer(n-1));
        CHECK(a.as_signed()==n); // nested decode must not overwrite this vector
        return value::signed_integer(inner.as_signed()+1);
    }});
    for(int n=0;n<20;++n)
        CHECK(r::round_trip_reusing(transport,client,recurse,value::signed_integer(n)).as_signed()==n);
    server.define("echo",reuse_test::echo{});
    const std::string original(65536,'z');
    auto owned=r::round_trip_reusing(transport,client,r::detail::symbol_id("echo"),value::borrowed_string(original));
    for(int i=0;i<20;++i) {
        const std::string overwrite(65536,'q');
        auto ignored=r::round_trip_reusing(transport,client,r::detail::symbol_id("echo"),value::borrowed_string(overwrite));
        CHECK(ignored.copy_string()==overwrite);
    }
    CHECK(owned.copy_string()==original);
    // A legacy/custom binder may retain a copy. Reuse must not silently turn
    // decoded strings into frame views, even when the binder takes const&.
    value retained;
    server.define("retain",retaining_binder{&retained});
    r::dispatch_workspace retained_ws;
    auto retained_frame=r::detail::encode_request_values(r::detail::symbol_id("retain"),value::borrowed_string(original));
    r::bytes retained_response;
    server.dispatch_into(retained_frame,retained_response,retained_ws);
    retained_frame.assign(retained_frame.size(),'q');
    retained_ws.release_cached_storage();
    CHECK(retained.copy_string()==original);
    CHECK(client.cached_request_capacity()<=1024 && client.cached_response_capacity()<=1024);
    // Scope destruction on exceptions must not expose active buffers to a nested call.
#if PORTAL_HAS_EXCEPTIONS
    server.define("throw",reuse_test::callback{[](value&)->reuse_test::result {throw std::runtime_error("expected");}});
    bool caught=false;
    try {r::round_trip_reusing(transport,client,r::detail::symbol_id("throw"),value::signed_integer(0));}
    catch(const r::remote_error&){caught=true;}
    CHECK(caught);
    CHECK(r::round_trip_reusing(transport,client,recurse,value::signed_integer(3)).as_signed()==3);
#endif
    r::dispatch_workspace args(1);
    r::bytes response;
    auto request=r::detail::encode_request_values(recurse,value::signed_integer(0),value::signed_integer(1));
    server.dispatch_into(request,response,args);
    CHECK(args.cached_argument_capacity()==0);
    request=r::detail::encode_request_values(recurse,value::signed_integer(0));
    server.dispatch_into(request,response,args);CHECK(args.cached_argument_capacity()==1);
    args.release_cached_storage();CHECK(args.cached_argument_capacity()==0);
    client.release_cached_storage();CHECK(client.cached_request_capacity()==0 && client.cached_response_capacity()==0);
    r::call_workspace no_cache(0);
    auto result=r::round_trip_reusing(transport,no_cache,recurse,value::signed_integer(0));
    CHECK(result.as_signed()==0 && no_cache.cached_request_capacity()==0 && no_cache.cached_response_capacity()==0);
}

void parallel() {
    r::router server;server.define("add",reuse_test::add{});
    std::atomic<bool> good{true};std::vector<std::thread> threads;
    for(int t=0;t<4;++t)threads.emplace_back([&,t]{
        r::call_workspace ws;r::reusing_loopback_transport transport(server);
        for(int i=0;i<10000;++i) {
            auto x=r::round_trip_reusing(transport,ws,r::detail::symbol_id("add"),
                value::signed_integer(i),value::signed_integer(t));
            if(x.as_signed()!=i+t)good.store(false);
        }
    });
    for(auto& t:threads) t.join();
    CHECK(good.load());
}

int main(){parity();malformed_and_alias();recursive_and_ownership();parallel();
    std::puts("PASS: wire parity, malformed frames, side effects, aliasing, recursion, ownership, bounds, 4-thread independent workspaces");}
