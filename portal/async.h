#ifndef PORTAL_ASYNC_H
#define PORTAL_ASYNC_H
#include "optional.h"
#include "error_policy.h"
#include <cassert>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <system_error>
#include <utility>
namespace portal {
struct async_error {
    std::error_code code;
    std::exception_ptr exception;
    async_error() noexcept = default;
    async_error(std::error_code c) noexcept : code(c) {}
    async_error(std::exception_ptr p) noexcept : exception(std::move(p)) {}
};
struct async_error_tag_t {};
constexpr async_error_tag_t async_error_tag{};
template<class R> struct async_result {
    static_assert(!std::is_reference<R>::value, "async results must own their storage");
    static_assert(std::is_nothrow_move_constructible<R>::value, "async results must move without throwing");
    optional<R> value;
    async_error error;
    explicit async_result(R r) noexcept(std::is_nothrow_move_constructible<R>::value)
        : value(in_place, std::move(r)) {}
    async_result(async_error_tag_t, async_error e) noexcept : error(std::move(e)) {}
    template<class V=R, typename std::enable_if<!std::is_same<V,async_error>::value,int>::type=0>
    explicit async_result(async_error e) noexcept : async_result(async_error_tag,std::move(e)) {}
    bool has_value() const noexcept { return bool(value); }
};
template<> struct async_result<void> {
    bool success;
    async_error error;
    async_result() noexcept : success(true) {}
    async_result(async_error_tag_t, async_error e) noexcept : success(false), error(std::move(e)) {}
    explicit async_result(async_error e) noexcept : async_result(async_error_tag,std::move(e)) {}
    bool has_value() const noexcept { return success; }
};
struct async_submission {
    bool accepted;
    async_error error;
    static async_submission accept() noexcept { return {true, {}}; }
    static async_submission reject(async_error e) noexcept { return {false, std::move(e)}; }
};
namespace async_detail {
inline void require(bool condition) noexcept {
    if (!condition) { assert(condition && "async invocation contract violated"); std::abort(); }
}
template<class State> class completion {
    std::shared_ptr<State> state_;
public:
    using result_type = typename State::result_type;
    explicit completion(std::shared_ptr<State> s) noexcept : state_(std::move(s)) {}
    // A copied token owns its control state. Raw pointers are not completion tokens.
    void finish(result_type r) const noexcept {
        // A continuation may destroy the callback holding this very token.
        // Keep the state alive until delivery and retirement have both returned.
        auto state = state_;
        state->finish(std::move(r));
    }
    void detach(async_error e) const noexcept {
        auto state = state_;
        state->detach(std::move(e));
    }
};
template<class R, class Context, class Op, class Handler>
struct operation_state {
    using result_type = async_result<R>;
    std::mutex mutex;
    bool delivered = false, retired = false, submitting = true, cleaned = false;
    optional<async_error> pending_detach;
    std::shared_ptr<Context> context;
    std::shared_ptr<void> access;
    optional<Op> operation;
    optional<Handler> handler;
    operation_state(std::shared_ptr<Context> c, Op op, Handler h, std::shared_ptr<void> a)
        : context(std::move(c)), access(std::move(a)), operation(in_place, std::move(op)),
          handler(in_place, std::move(h)) {}
    void deliver(result_type r, bool retire) noexcept {
        optional<Handler> callback;
        bool consume_handler=false, clean=false;
        std::shared_ptr<void> release_access;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!retire && submitting) {
                if (!pending_detach) pending_detach.emplace(std::move(r.error));
                return;
            }
            if (retire) {
                if (retired) return;
                retired = true;
                release_access = std::move(access);
            }
            if (!delivered) {
                delivered = true;
                consume_handler=true;
            }
            clean = claim_cleanup_locked();
        }
        // The delivered flag grants exclusive access to the slot. Even moved-from
        // user callable destructors must run outside the mutex.
        if(consume_handler) {
            callback.emplace(std::move(*handler));
            handler.reset(); // A moved-from callable is not necessarily empty.
        }
        // Borrow release and arbitrary destructors/continuations are outside the lock.
        release_access.reset();
        if (callback) {
#if PORTAL_HAS_EXCEPTIONS
            try { (*callback)(std::move(r)); } catch (...) { std::terminate(); }
#else
            (*callback)(std::move(r));
#endif
        }
        cleanup(clean);
    }
    void finish(result_type r) noexcept { deliver(std::move(r), true); }
    void detach(async_error e) noexcept { deliver(result_type(async_error_tag,std::move(e)), false); }
    // Called only while the transition mutex is held. Claiming cleanup here
    // avoids reacquiring it after every submission and completion, including
    // transitions that cannot yet retire anything.
    bool claim_cleanup_locked() noexcept {
        if (!retired || submitting || cleaned) return false;
        cleaned=true;
        return true;
    }
    void cleanup(bool claimed) noexcept {
        if (!claimed) return;
        // Only the cleanup claimant touches these slots after submit returns.
        operation.reset();
        context.reset();
    }
    void reject(async_error e) noexcept {
        { std::lock_guard<std::mutex> lock(mutex); require(!retired); }
        finish(result_type(async_error_tag,std::move(e)));
    }
    void submitted() noexcept {
        optional<async_error> deferred;
        bool clean;
        { std::lock_guard<std::mutex> lock(mutex); submitting = false;
          if (pending_detach) { deferred.emplace(std::move(*pending_detach)); pending_detach.reset(); }
          clean = claim_cleanup_locked(); }
        if (deferred) detach(std::move(*deferred));
        cleanup(clean);
    }
};
}
// Static performance path: Context, operation and continuation remain concrete types.
template<class R, class Context, class Op> class static_invocation {
    static_assert(std::is_nothrow_move_constructible<Op>::value, "operation must move without throwing");
    std::shared_ptr<Context> context_;
    std::shared_ptr<void> access_;
    optional<Op> operation_;
public:
    using result_type = R;
    static_invocation(std::shared_ptr<Context> c, Op op, std::shared_ptr<void> a)
        : context_(std::move(c)), access_(std::move(a)), operation_(in_place, std::move(op)) {
        async_detail::require(bool(context_));
    }
    static_invocation(static_invocation&&) noexcept = default;
    static_invocation& operator=(static_invocation&& other) noexcept {
        if(this!=&other) { this->~static_invocation(); new(this) static_invocation(std::move(other)); }
        return *this;
    }
    static_invocation(const static_invocation&) = delete;
    template<class Handler> void start(Handler&& handler) && {
        using H = typename std::decay<Handler>::type;
        using State = async_detail::operation_state<R, Context, Op, H>;
        using Token = async_detail::completion<State>;
        async_detail::require(bool(operation_) && bool(context_));
#if !PORTAL_HAS_EXCEPTIONS && PORTAL_NO_EXCEPTION_STRICT
        static_assert(std::is_nothrow_constructible<H,Handler&&>::value, "strict completion capture must be noexcept");
        static_assert(noexcept(std::declval<Op&>()(std::declval<Context&>(), std::declval<Token>())),
                      "strict async operation submission must be noexcept");
        static_assert(noexcept(std::declval<H&>()(std::declval<async_result<R>>())),
                      "strict async completion must be noexcept");
#endif
        static_assert(std::is_nothrow_move_constructible<Op>::value, "operation must move without throwing");
        static_assert(std::is_nothrow_move_constructible<H>::value, "completion must move without throwing");
        auto state = std::make_shared<State>(std::move(context_), std::move(*operation_),
                                            std::forward<Handler>(handler), std::move(access_));
        operation_.reset();
#if PORTAL_HAS_EXCEPTIONS
        try {
#endif
            auto submitted = (*state->operation)(*state->context, Token(state));
            if (!submitted.accepted) state->reject(std::move(submitted.error));
#if PORTAL_HAS_EXCEPTIONS
        } catch (...) { state->reject(async_error(std::current_exception())); }
#endif
        state->submitted();
    }
};
template<class R, class Context, class Op>
auto make_invocation(std::shared_ptr<Context> owner, Op&& op, std::shared_ptr<void> access = {}) {
    using O = typename std::decay<Op>::type;
#if !PORTAL_HAS_EXCEPTIONS && PORTAL_NO_EXCEPTION_STRICT
    static_assert(std::is_nothrow_constructible<O,Op&&>::value, "strict operation capture must be noexcept");
#endif
    return static_invocation<R, Context, O>(std::move(owner), std::forward<Op>(op), std::move(access));
}
// Optional erasure boundary. Static callers never instantiate this machinery.
template<class R> class invocation {
    using handler_type = std::function<void(async_result<R>)>;
    struct base { virtual ~base() = default; virtual void start(handler_type) = 0; };
    template<class Call> struct model final : base {
        Call call;
        explicit model(Call c) : call(std::move(c)) {}
        void start(handler_type h) override {
            std::move(call).start([h=std::move(h)](async_result<R> r) mutable noexcept { h(std::move(r)); });
        }
    };
    std::unique_ptr<base> operation_;
public:
    using result_type = R;
    template<class C, class O> explicit invocation(static_invocation<R,C,O>&& call)
        : operation_(new model<static_invocation<R,C,O>>(std::move(call))) {}
    invocation(invocation&&) noexcept = default;
    invocation& operator=(invocation&&) noexcept = default;
    invocation(const invocation&) = delete;
    template<class Handler> void start(Handler&& handler) && {
#if !PORTAL_HAS_EXCEPTIONS && PORTAL_NO_EXCEPTION_STRICT
        using H=typename std::decay<Handler>::type;
        static_assert(std::is_nothrow_constructible<H,Handler&&>::value, "strict completion capture must be noexcept");
        static_assert(noexcept(std::declval<H&>()(std::declval<async_result<R>>())),
                      "strict erased completion must be noexcept before erasure");
#endif
        async_detail::require(bool(operation_));
        auto op = std::move(operation_);
        op->start(handler_type(std::forward<Handler>(handler)));
    }
};
template<class Call> auto erase_invocation(Call&& call) {
    return invocation<typename std::decay<Call>::type::result_type>(std::forward<Call>(call));
}
}
#endif
