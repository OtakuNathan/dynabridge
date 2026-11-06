#ifndef DYNABRIDGE_ASYNC_IMPORT_H
#define DYNABRIDGE_ASYNC_IMPORT_H
#include "async.h"
#include "import_core.h"
#include <string>
#include <tuple>
namespace dynabridge {
// User-defined views/pointers/callbacks require an explicit owning capture policy.
template<class T, class Enable=void> struct async_capture;
template<class T> struct async_capture<T, typename std::enable_if<std::is_arithmetic<T>::value || std::is_enum<T>::value>::type> {
    template<class U> static T capture(U&& value) noexcept { return std::forward<U>(value); }
};
template<> struct async_capture<std::string> {
    template<class U> static std::string capture(U&& value) noexcept(std::is_nothrow_constructible<std::string,U&&>::value) {
        return std::string(std::forward<U>(value));
    }
};
// Explicit owner coverage is a backend/caller contract, not a property proved by aliasing.
template<class T> struct retained_argument {
    std::shared_ptr<T> owner;
    explicit retained_argument(std::shared_ptr<T> p) noexcept : owner(std::move(p)) { async_detail::require(bool(owner)); }
    operator const T&() const noexcept { return *owner; }
    template<class... A> auto operator()(A&&... a) const noexcept(noexcept((*owner)(std::forward<A>(a)...)))
        -> decltype((*owner)(std::forward<A>(a)...)) { return (*owner)(std::forward<A>(a)...); }
};
template<class T> auto retain_argument(std::shared_ptr<T> owner) noexcept { return retained_argument<T>(std::move(owner)); }
template<class T> struct async_capture<retained_argument<T>> {
    static retained_argument<T> capture(retained_argument<T> value) noexcept { return value; }
};
namespace async_detail {
template<class T> auto capture(T&& value) -> decltype(async_capture<typename std::decay<T>::type>::capture(std::forward<T>(value))) {
#if !DYNABRIDGE_HAS_EXCEPTIONS && DYNABRIDGE_NO_EXCEPTION_STRICT
    using Captured=decltype(async_capture<typename std::decay<T>::type>::capture(std::forward<T>(value)));
    static_assert(std::is_nothrow_move_constructible<Captured>::value, "strict captured storage must move without throwing");
    static_assert(noexcept(async_capture<typename std::decay<T>::type>::capture(std::forward<T>(value))),
                  "strict async capture must be noexcept; move owned values instead of copying");
#endif
    return async_capture<typename std::decay<T>::type>::capture(std::forward<T>(value));
}
template<bool Erased> struct projection;
template<> struct projection<false> { template<class C> static C apply(C c) { return c; } };
template<> struct projection<true> { template<class C> static auto apply(C c) { return erase_invocation(std::move(c)); } };
template<class... Args> struct safe_signature;
template<> struct safe_signature<> : std::true_type {};
template<class A,class... Rest> struct safe_signature<A,Rest...>
    : std::integral_constant<bool,(!std::is_lvalue_reference<A>::value ||
        std::is_const<typename std::remove_reference<A>::type>::value) && safe_signature<Rest...>::value> {};
template<class Contract> struct contract_result;
template<class R,class... Args> struct contract_result<free_callable<R(Args...)>> {
    static_assert(!std::is_reference<R>::value, "bridge callable return type cannot be a reference; use a value or void instead.");
    static_assert(safe_signature<Args...>::value, "async non-const reference parameters require explicit output semantics");
    using type=R;
};
template<bool Erased,class Contract,class Call> auto checked_projection(Call call) {
    static_assert(std::is_same<typename Call::result_type,typename contract_result<Contract>::type>::value,
                  "async backend result must match the selected schema signature");
    return projection<Erased>::apply(std::move(call));
}
template<class Descriptor,class Host> struct mixin {};
template<class Host,class List> struct interfaces;
template<class Host,class... D> struct interfaces<Host,type_list<D...>> : mixin<D,Host>... {};
template<class Delegate> struct owned_receiver {
    using Context=typename Delegate::context_t;
    std::shared_ptr<Context> context;
    Delegate receiver;
    owned_receiver(std::shared_ptr<Context> c, typename Delegate::object_t object)
        : context(std::move(c)), receiver(*context,std::move(object)) {}
};
template<class Delegate,bool Erased> class receiver_storage {
    std::shared_ptr<Delegate> receiver_;
public:
    using Context=typename Delegate::context_t;
    receiver_storage(std::shared_ptr<Context> owner, typename Delegate::object_t object) {
        require(bool(owner));
        auto box=std::make_shared<owned_receiver<Delegate>>(std::move(owner),std::move(object));
        receiver_=std::shared_ptr<Delegate>(box,&box->receiver);
    }
    template<class Symbol,class... Args> auto prepare(Args&&... args) const {
        static_assert(std::is_same<typename Symbol::receiver_symbol_t,typename Delegate::bridge_class_t>::value,
                      "async member symbol belongs to a different receiver");
        // Resolve using original expressions BEFORE owning capture changes value categories.
        using selector=import_contract_selector_group<typename Symbol::overloads_t,Context>;
        using contract=typename decltype(selector::select(std::declval<Args>()...))::type;
        auto arguments=std::make_tuple(capture(std::forward<Args>(args))...);
        return checked_projection<Erased,contract>(Context::backend_t::template prepare_async<Symbol,contract>(receiver_,std::move(arguments)));
    }
};
}
template<template<class> class Delegate> struct async_class_traits;
template<template<class> class Delegate,class Context,class Object>
auto bind_async(std::shared_ptr<Context> owner,Object&& object) {
    return async_class_traits<Delegate>::template bind<true>(std::move(owner),std::forward<Object>(object));
}
template<template<class> class Delegate,class Context,class Object>
auto bind_static_async(std::shared_ptr<Context> owner,Object&& object) {
    return async_class_traits<Delegate>::template bind<false>(std::move(owner),std::forward<Object>(object));
}
}
#endif
