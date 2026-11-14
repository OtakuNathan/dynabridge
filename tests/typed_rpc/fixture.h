#ifndef PORTAL_TYPED_RPC_TEST_FIXTURE_H
#define PORTAL_TYPED_RPC_TEST_FIXTURE_H
#include "portal/rpc/typed.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#__VA_ARGS__); std::abort(); } } while(false)
namespace w = portal::wire;
namespace tr = portal::typed_rpc;
struct Packet {
    std::uint64_t sequence = 0;
    std::string payload;
    std::vector<std::uint32_t> samples;
    w::option<double> scale;
    std::array<std::uint16_t, 3> dimensions{};
};
#define PACKET_FIELDS(X,T) \
    X(T, 1, sequence, w::u64) \
    X(T, 2, payload, w::blob<4096>) \
    X(T, 3, samples, w::sequence<w::u32, 64>) \
    X(T, 4, scale, w::maybe<w::f64>) \
    X(T, 5, dimensions, w::array<w::u16, 3>)
PORTAL_WIRE_RECORD(Packet, 0x1001, 1, PACKET_FIELDS);
#define PORTAL_TYPED_METHOD(Name, Id, Revision, ...) \
    using Name = tr::method<Id, Revision, __VA_ARGS__>;
#include "methods.def"
#undef PORTAL_TYPED_METHOD
static_assert(std::is_same<Add::signature_t, std::int32_t(std::int32_t, std::int32_t)>::value,
              "method description must project its C++ contract");
static_assert(std::is_same<Ping::signature_t, void()>::value, "none projects void");
inline Packet packet() {
    Packet p; p.sequence=0x0123456789abcdefull;
    p.payload=std::string("hi\0\xff",4); p.samples={0,1,0xffffffffu};
    p.scale.present=true;p.scale.value=-0.125;p.dimensions={{3,7,65535}};return p;
}
inline bool same(const Packet& a,const Packet& b) {
    return a.sequence==b.sequence && a.payload==b.payload && a.samples==b.samples
        && a.scale.present==b.scale.present && (!a.scale.present||a.scale.value==b.scale.value)
        && a.dimensions==b.dimensions;
}
inline void bind(tr::router& r) {
    CHECK(r.define<Add>([](std::int32_t a,std::int32_t b) noexcept {return a+b;})==w::errc::ok);
    CHECK(r.define<Echo>([](std::string x) noexcept {return x;})==w::errc::ok);
    CHECK(r.define<EchoPacket>([](Packet x) noexcept {return x;})==w::errc::ok);
    CHECK(r.define<Ping>([]() noexcept {})==w::errc::ok);
}
#endif
