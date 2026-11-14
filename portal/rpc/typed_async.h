#ifndef PORTAL_RPC_TYPED_ASYNC_H
#define PORTAL_RPC_TYPED_ASYNC_H
#include "typed.h"
#include "../async.h"

namespace portal { namespace typed_rpc {
class error_category final : public std::error_category {
public:
    const char* name() const noexcept override { return "portal.typed_rpc"; }
    std::string message(int e) const override {
        switch (static_cast<errc>(e)) {
        case errc::ok: return "success";
        case errc::malformed: return "malformed typed RPC frame";
        case errc::limit_exceeded: return "typed RPC limit exceeded";
        case errc::invalid_value: return "invalid typed RPC value";
        case errc::unknown_method: return "unknown typed RPC method";
        case errc::incompatible_schema: return "incompatible typed RPC schema";
        case errc::handler_failed: return "typed RPC handler failed";
        case errc::transport_failed: return "typed RPC transport failed";
        }
        return "unknown typed RPC error";
    }
};
inline std::error_code make_error_code(errc e) noexcept {
    static error_category category;
    return std::error_code(static_cast<int>(e), category);
}
namespace detail {
template<class Method, class Token> struct response_completion {
    Token token;
    limits budget;
    // Transport moves the final response here. Decoding finishes before the
    // frame is destroyed; no view escapes into an FF continuation.
    void operator()(bytes response, async_error error = {}) const noexcept {
        using R = typename Method::result_type;
        if (error.code || error.exception) {
            token.finish(async_result<R>(async_error_tag, std::move(error))); return;
        }
#if PORTAL_HAS_EXCEPTIONS
        try {
#endif
            auto decoded = decode_response<Method>(view(response), budget);
            if (decoded) token.finish(async_result<R>(std::move(decoded.value)));
            else token.finish(async_result<R>(async_error_tag, make_error_code(decoded.error)));
#if PORTAL_HAS_EXCEPTIONS
        } catch (...) {
            token.finish(async_result<R>(async_error_tag, std::current_exception()));
        }
#endif
        // Do not access *this after finish: the handler can destroy this callback.
    }
};
template<class Method> struct request_operation {
    bytes request;
    limits budget;
    errc preparation;
    template<class Transport, class Token>
    async_submission operator()(Transport& transport, Token token)
        noexcept(noexcept(transport.async_round_trip(
            std::declval<bytes>(), std::declval<response_completion<Method, Token>>()))) {
        if (preparation != errc::ok) return async_submission::reject(make_error_code(preparation));
        // The transport takes ownership of the byte vector. No worker-local
        // workspace is lent to an asynchronous operation.
        return transport.async_round_trip(std::move(request),
            response_completion<Method, Token>{std::move(token), budget});
    }
};
}
// The existing static_invocation is the one owner of delivery/retirement state.
// No additional std::function/shared_ptr state is introduced by this adapter.
// Accepted transport submissions must make exactly one terminal callback;
// rejected/throwing submissions must retain no callback and never call it.
// A callback can execute inline, including before async_round_trip returns.
template<class Method, class Transport, class... A>
auto make_rpc_invocation(std::shared_ptr<Transport> transport, limits budget, const A&... args) {
    bytes request;
    const auto status = encode_request<Method>(request, budget, args...);
    return portal::make_invocation<typename Method::result_type>(std::move(transport),
        detail::request_operation<Method>{std::move(request), budget, status});
}
}}
#endif
