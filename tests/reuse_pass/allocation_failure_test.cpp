#include "fixtures.h"
#include <cstdio>
#include <cstdlib>
#include <new>

// Fault injection is intentionally a separate, single-threaded contract binary.
// It is never linked into a timing binary or into Portal itself.
#if PORTAL_HAS_EXCEPTIONS
namespace {
long fail_after = -1;
bool failed = false;
bool reject_allocation() noexcept {
    if (fail_after < 0) return false;
    if (fail_after-- == 0) { fail_after = -1; failed = true; return true; }
    return false;
}
}
void* operator new(std::size_t size) {
    if (reject_allocation()) throw std::bad_alloc();
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#endif
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr,"CHECK: %s line %d\n",#c,__LINE__); std::abort(); } } while (false)

int main() {
#if PORTAL_HAS_EXCEPTIONS
    namespace r = portal::rpc;
    using reuse_test::value;
    r::router server;
    server.define("echo", reuse_test::echo{});
    const auto id = r::detail::symbol_id("echo");
    const std::string payload(1024, 'p');
    unsigned hit = 0;
    for (long failure = 0; failure < 16; ++failure) {
        // Fresh workspaces include cold growth in the fault-injection coverage.
        r::call_workspace workspace(2048);
        r::reusing_loopback_transport transport(server);
        failed = false; fail_after = failure;
        try {
            auto answer = r::round_trip_reusing(transport, workspace, id,
                                               value::borrowed_string(payload));
            CHECK(answer.string_size() == payload.size());
        } catch (const std::bad_alloc&) {
        } catch (const r::remote_error&) {
            // Server-side allocation exceptions use the existing remote-error
            // encoding. This is not a rollback guarantee for callable effects.
        }
        fail_after = -1;
        hit += failed ? 1u : 0u;
        auto answer = r::round_trip_reusing(transport, workspace, id,
                                           value::borrowed_string(payload));
        CHECK(answer.copy_string() == payload);
        CHECK(workspace.cached_request_capacity() <= 2048);
        CHECK(workspace.cached_response_capacity() <= 2048);
    }
    CHECK(hit >= 5); // request frame, argument vector/string, response frame/string
    r::call_workspace workspace(1024);
    struct failing_transport {
        void round_trip_into(const r::bytes&, r::bytes& response) {
            response.resize(128);
            throw std::runtime_error("transport failure");
        }
    } failing;
    bool caught = false;
    try { r::round_trip_reusing(failing, workspace, id, value::signed_integer(1)); }
    catch (const std::runtime_error&) { caught = true; }
    CHECK(caught && workspace.cached_request_capacity() != 0);
    CHECK(workspace.cached_response_capacity() >= 128);
    struct malformed_transport {
        void round_trip_into(const r::bytes&, r::bytes& response) {
            response.assign(8, 0);
        }
    } malformed;
    caught = false;
    try { r::round_trip_reusing(malformed, workspace, id, value::signed_integer(1)); }
    catch (const r::protocol_error&) { caught = true; }
    CHECK(caught);
    r::reusing_loopback_transport good(server);
    CHECK(r::round_trip_reusing(good, workspace, id, value::borrowed_string(payload)).copy_string() == payload);
    std::printf("PASS: %u allocation-failure points, throwing/malformed transports, subsequent reuse\n", hit);
#else
    std::puts("SKIP: exception-based allocation injection requires exceptions");
    return 77;
#endif
}
