#ifndef PORTAL_BACKENDS_RPC_REUSE_H
#define PORTAL_BACKENDS_RPC_REUSE_H

#include "rpc.h"

namespace portal { namespace rpc {

// This cache is worker-confined, not a shared request queue. Each scope owns its
// active buffers; recursive calls use separate storage. max_retained_bytes is a
// capacity bound per cached buffer, not an input-size limit or live-memory bound.
class call_workspace {
    bytes request_cache_, response_cache_;
    const std::size_t max_retained_bytes_;

    void recycle(bytes& buffer, bytes& cache) noexcept {
        buffer.clear();
        if (buffer.capacity() <= max_retained_bytes_ && buffer.capacity() > cache.capacity())
            buffer.swap(cache);
    }
public:
    explicit call_workspace(std::size_t max_retained_bytes = 64 * 1024) noexcept
        : max_retained_bytes_(max_retained_bytes) {}
    call_workspace(const call_workspace&) = delete;
    call_workspace& operator=(const call_workspace&) = delete;

    class scope {
        call_workspace& owner_;
    public:
        bytes request, response;
        explicit scope(call_workspace& owner) noexcept : owner_(owner) {
            request.swap(owner_.request_cache_);
            response.swap(owner_.response_cache_);
        }
        scope(const scope&) = delete;
        scope& operator=(const scope&) = delete;
        ~scope() noexcept {
            owner_.recycle(request, owner_.request_cache_);
            owner_.recycle(response, owner_.response_cache_);
        }
    };

    std::size_t cached_request_capacity() const noexcept { return request_cache_.capacity(); }
    std::size_t cached_response_capacity() const noexcept { return response_cache_.capacity(); }
    void release_cached_storage() noexcept {
        bytes().swap(request_cache_);
        bytes().swap(response_cache_);
    }
};

// Transport must synchronously finish reading request and fill response before
// returning. Neither reference may escape round_trip_into(). Use an owning
// per-operation buffer for asynchronous transports instead.
template<class Transport, class... Values>
value round_trip_reusing(Transport& transport, call_workspace& workspace,
                         std::uint64_t method, const Values&... args) {
    call_workspace::scope buffers(workspace);
    detail::encode_request_values_into(buffers.request, method, args...);
    transport.round_trip_into(buffers.request, buffers.response);
    // decode_response returns an owning value; no frame view escapes the scope.
    return detail::decode_response(buffers.response);
}

// Opt-in, thread-confined loopback adapter; ordinary loopback_transport is not
// changed. Multiple adapters may use the same define-then-serve router provided
// its bound callables are themselves safe for concurrent use.
class reusing_loopback_transport {
    router& router_;
    dispatch_workspace workspace_;
public:
    explicit reusing_loopback_transport(router& server, std::size_t retained_args = 64) noexcept
        : router_(server), workspace_(retained_args) {}
    void round_trip_into(const bytes& request, bytes& response) {
        router_.dispatch_into(request, response, workspace_);
    }
};

}} // namespace portal::rpc
#endif
