#include "portal/portal.h"
#include "portal/backends/rpc.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::abort(); } } while(false)

static void check_wire_format() {
    namespace codec = portal::rpc::detail;
    using portal::rpc::bytes;
    using value = portal::rpc::value;
    // Exercise growth as well as the exactly reserved encoders. Integers are
    // little endian on the wire, independently of the native byte order.
    codec::writer growing;
    growing.integer<std::uint8_t>(0xab);
    growing.integer<std::uint32_t>(0x12345678u);
    growing.integer<std::uint64_t>(0x1020304050607080ull);
    CHECK(growing.finish() == bytes({0xab, 0x78, 0x56, 0x34, 0x12,
        0x80, 0x70, 0x60, 0x50, 0x40, 0x30, 0x20, 0x10}));

    const std::uint64_t nan_bits = 0x7ff8000000001234ull;
    double nan_value;
    std::memcpy(&nan_value, &nan_bits, sizeof(nan_value));
    const std::vector<value> arguments{
        {}, value::signed_integer(std::numeric_limits<std::int64_t>::min()),
        value::unsigned_integer(std::numeric_limits<std::uint64_t>::max()),
        value::real(nan_value), value::boolean(true),
        value::string(std::string("a\0b\0c", 5)), value::boolean(false)};
    const bytes expected{
        0x44, 0x52, 0x50, 0x43, 1,
        0x80, 0x70, 0x60, 0x50, 0x40, 0x30, 0x20, 0x10, 7, 0, 0, 0,
        0,
        1, 0, 0, 0, 0, 0, 0, 0, 0x80,
        2, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        3, 0x34, 0x12, 0, 0, 0, 0, 0xf8, 0x7f,
        4, 1,
        5, 5, 0, 0, 0, 'a', 0, 'b', 0, 'c',
        4, 0};
    CHECK(codec::encode_request(0x1020304050607080ull, arguments) == expected);
    CHECK(codec::encode_request_values(0x1020304050607080ull,
        arguments[0], arguments[1], arguments[2], arguments[3],
        arguments[4], arguments[5], arguments[6]) == expected);
    codec::request parsed;
    CHECK(codec::try_decode_request(expected, parsed));
    CHECK(parsed.method == 0x1020304050607080ull && parsed.args.size() == 7);
    CHECK(codec::encode_request(parsed.method, parsed.args) == expected);
    for (std::size_t size = 0; size < expected.size(); ++size) {
        CHECK(!codec::try_decode_request(bytes(expected.begin(), expected.begin() + size), parsed));
        CHECK(codec::encode_request(parsed.method, parsed.args) == expected);
    }
    auto malformed = expected;
    for (std::size_t i = 13; i < 17; ++i) malformed[i] = 0xff;
    CHECK(!codec::try_decode_request(malformed, parsed)); // Huge argc, short frame.
    malformed = codec::encode_request_values(1, value::string("x"));
    for (std::size_t i = 18; i < 22; ++i) malformed[i] = 0xff;
    CHECK(!codec::try_decode_request(malformed, parsed)); // Huge string, short frame.
    CHECK(codec::encode_request(parsed.method, parsed.args) == expected);

    value output;
    std::string error = "unchanged";
    CHECK(codec::encode_response({}) == bytes({0x44, 0x52, 0x50, 0x52, 1, 0, 0}));
    auto response = codec::encode_response(value::boolean(true));
    response.back() = 0xfe; // The existing protocol accepts every nonzero bool byte.
    CHECK(codec::try_decode_response(response, output, error) == codec::response_status::ok);
    CHECK(output.as_boolean() && error == "unchanged");
    response[5] = 2;
    CHECK(codec::try_decode_response(response, output, error) == codec::response_status::malformed);
    CHECK(output.as_boolean() && error == "unchanged");
    response = codec::encode_error(std::string("a\0b", 3));
    CHECK(response == bytes({0x44, 0x52, 0x50, 0x52, 1, 1, 3, 0, 0, 0, 'a', 0, 'b'}));
    for (std::size_t size = 0; size < response.size(); ++size) {
        CHECK(codec::try_decode_response(bytes(response.begin(), response.begin() + size), output, error)
            == codec::response_status::malformed);
        CHECK(output.as_boolean() && error == "unchanged");
    }
    CHECK(codec::try_decode_response(response, output, error) == codec::response_status::remote_error);
    CHECK(output.as_boolean() && error == std::string("a\0b", 3));
}

int main() {
    check_wire_format();
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
