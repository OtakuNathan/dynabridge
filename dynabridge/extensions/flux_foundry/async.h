#ifndef DYNABRIDGE_FF_ASYNC_H
#define DYNABRIDGE_FF_ASYNC_H
#include <dynabridge/async.h>
#include <flow/flow.h>
namespace flux_foundry {
template<> struct awaitable_creating_error<dynabridge::async_error> {
    static dynabridge::async_error make() noexcept {return std::make_error_code(std::errc::not_enough_memory);}
};
template<> struct async_submission_failed_error<dynabridge::async_error> {
    static dynabridge::async_error make() noexcept {return std::make_error_code(std::errc::resource_unavailable_try_again);}
};
template<> struct cancel_error<dynabridge::async_error> {
    static dynabridge::async_error make(cancel_kind) noexcept {return std::make_error_code(std::errc::operation_canceled);}
};
}
namespace dynabridge { namespace ff {
namespace detail {
template<class R> struct forward_result {
    template<class A> static void finish(A* awaitable,async_result<R> r) noexcept {
        using out=typename A::async_result_type;
        if(r.has_value()) awaitable->resume(out(flux_foundry::value_tag,std::move(*r.value)));
        else awaitable->resume(out(flux_foundry::error_tag,std::move(r.error)));
    }
};
template<> struct forward_result<void> {
    template<class A> static void finish(A* awaitable,async_result<void> r) noexcept {
        using out=typename A::async_result_type;
        if(r.has_value()) awaitable->resume(out(flux_foundry::value_tag));
        else awaitable->resume(out(flux_foundry::error_tag,std::move(r.error)));
    }
};
}
template<class Call> struct invocation_awaitable final
    : flux_foundry::fast_awaitable_base<invocation_awaitable<Call>,typename Call::result_type,async_error> {
    using R=typename Call::result_type;
    using async_result_type=flux_foundry::result_t<R,async_error>;
    optional<Call> call;
    async_error input_error;
    template<class Input> explicit invocation_awaitable(Input&& input) noexcept {
        if(input.has_value()) call.emplace(std::move(input.value()));
        else input_error=std::move(input.error());
    }
    bool available() noexcept {return true;}
    int submit() noexcept {
#if DYNABRIDGE_HAS_EXCEPTIONS
        try {
#endif
            if(!call) {this->resume(async_result_type(flux_foundry::error_tag,std::move(input_error)));return 0;}
            // A local copy keeps the invocation alive across an inline continuation.
            auto operation=std::move(*call);call.reset();
            std::move(operation).start([this](async_result<R> r) noexcept { detail::forward_result<R>::finish(this,std::move(r)); });
#if DYNABRIDGE_HAS_EXCEPTIONS
        } catch (...) { this->resume(async_result_type(flux_foundry::error_tag,std::current_exception())); }
#endif
        // Typed rejection already completed the FF protocol, preserving its precise error.
        return 0;
    }
};
template<class R> auto await_invocation() noexcept {
    return flux_foundry::await<invocation_awaitable<invocation<R>>>();
}
template<class Call> auto await_static_invocation() noexcept {
    return flux_foundry::await<invocation_awaitable<Call>>();
}
template<class R,class Executor> auto await_invocation(Executor&& executor) noexcept {
    return flux_foundry::await<invocation_awaitable<invocation<R>>>(std::forward<Executor>(executor));
}
template<class Call,class Executor> auto await_static_invocation(Executor&& executor) noexcept {
    return flux_foundry::await<invocation_awaitable<Call>>(std::forward<Executor>(executor));
}
// Convenience runner; a reusable Blueprint can instead accept each fresh Call as input.
template<class R,class Handler> struct invocation_sink {
    using value_type=flux_foundry::result_t<R,async_error>;
    std::shared_ptr<optional<Handler>> handler;
    void emplace(value_type&& result) noexcept {
        if(!*handler) return;
        auto done=std::move(**handler);handler->reset();
        done(std::move(result));
    }
};
template<class Call,class Handler> void start_invocation(Call call,Handler&& handler) {
    using H=typename std::decay<Handler>::type;
    using R=typename Call::result_type;
    auto slot=std::make_shared<optional<H>>(in_place,std::forward<Handler>(handler));
    using output=flux_foundry::result_t<R,async_error>;
    auto bp=flux_foundry::make_blueprint<Call,async_error>() | await_static_invocation<Call>()
        | flux_foundry::end([](output&& r) noexcept -> output {return std::move(r);});
    auto owned=flux_foundry::make_lite_ptr<decltype(bp)>(std::move(bp));
    if(!owned) {
        {auto abandoned=std::move(call);}
        invocation_sink<R,H>{slot}.emplace(flux_foundry::result_t<R,async_error>(flux_foundry::error_tag,
            std::make_error_code(std::errc::not_enough_memory)));return;
    }
    auto runner=flux_foundry::make_fast_runner(owned,invocation_sink<R,H>{slot});
    runner(std::move(call));
}

}}
#endif
