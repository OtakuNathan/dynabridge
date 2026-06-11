#ifndef DYNABRIDGE_IMPORT_CALLABLE_H
#define DYNABRIDGE_IMPORT_CALLABLE_H

#include <utility>

#include "backend_base.h"
#include "callable.h"

namespace dynabridge {
    template <typename Context, typename U = void>
    struct is_context_legal : std::false_type {
    };

    template <typename Context>
    struct is_context_legal<Context, void_t<typename Context::backend_t>>
        : std::is_base_of<backend_base<typename Context::backend_t>, typename Context::backend_t> {
    };

    template <typename Context, typename Receiver, typename... Args>
    struct is_forward_invocable {
        template <typename T, typename = void>
        struct probe : std::false_type {
        };

        template <typename T>
        struct probe<T, void_t<
            decltype(to_cast<Receiver>(
                std::declval<T&>(), std::declval<Receiver&>())),
            decltype(to_cast<Args>(
                std::declval<T&>(), std::declval<Args>()))...>>
            : std::true_type {
        };

        constexpr static bool value =
            are_bridge_params_valid<Args...>::value && probe<Context>::value;
    };

    template <typename Context, typename... Args>
    struct is_forward_invocable<Context, no_receiver_t, Args...> {
        template <typename T, typename = void>
        struct probe : std::false_type {
        };

        template <typename T>
        struct probe<T, void_t<decltype(to_cast<Args>(
                                std::declval<T&>(), std::declval<Args>()))...>>
            : std::true_type {
        };

        constexpr static bool value =
            are_bridge_params_valid<Args...>::value && probe<Context>::value;
    };

    template <typename Context, typename R, typename U = void>
    struct is_forward_result_convertible : std::true_type {
    };

    template <typename Context, typename U>
    struct is_forward_result_convertible<Context, void, U> : std::true_type {
    };

    template <typename Context, typename R>
    struct is_forward_result_convertible<
        Context,
        R,
        void_t<backend_dynamic_value_t<typename Context::backend_t>>> {
        template <typename T, typename = void>
        struct probe : std::false_type {
        };

        template <typename T>
        struct probe<T, void_t<
            decltype(from_cast<R>(
                std::declval<T&>(),
                std::declval<backend_dynamic_value_t<typename T::backend_t>>()))>>
            : std::true_type {
        };

        constexpr static bool value = probe<Context>::value;
    };

    template <typename Context, typename Receiver, typename... Args>
    struct is_forward_constructible {
        template <typename T, typename = void>
        struct probe : std::false_type {
        };

        template <typename T>
        struct probe<T, void_t<
            decltype(to_cast<typename std::decay<Args>::type>(
                    std::declval<T&>(), std::declval<Args>()))...,
            decltype(typename T::backend_t::template object_t<Receiver, import_t>(
                std::declval<T&>(),
                construct_object,
                std::declval<Args>()...))>>
            : std::true_type {
        };

        constexpr static bool value =
            are_bridge_params_valid<Args...>::value && probe<Context>::value;
    };

    template <typename Contract>
    struct import_callable;

    template <typename Receiver, typename R, typename... Args>
    struct import_callable<callable<Receiver, R(Args...)>> {
        using receiver_t = Receiver;
        using signature_t = R(Args...);

        template <typename Context, typename R_ = R,
            std::enable_if_t<!is_void_v<R_>>* = nullptr>
        static R_ invoke(Context& ctx, Receiver& receiver, Args... args) {
            static_assert(is_context_legal<Context>::value,
                "Your Context must define backend_t and backend_t must derive from backend_base<backend_t>.");
            static_assert(is_forward_invocable<Context, Receiver, Args...>::value,
                "Your forward binding is not convertible. This might be because "
                "converter<Receiver>::to is missing for the receiver, or converter<T>::to "
                "is missing for an argument.");
            static_assert(is_forward_result_convertible<Context, R>::value,
                "Your forward binding result is not convertible. This might be because "
                "converter<R>::from is missing for the backend dynamic value.");
            return Context::backend_t::template invoke<R>(
                ctx, receiver, std::move(args)...);
        }

        template <typename Context, typename R_ = R,
            std::enable_if_t<is_void_v<R_>>* = nullptr>
        static void invoke(Context& ctx, Receiver& receiver, Args... args) {
            static_assert(is_context_legal<Context>::value,
                "Your Context must define backend_t and backend_t must derive from backend_base<backend_t>.");
            static_assert(is_forward_invocable<Context, Receiver, Args...>::value,
                "Your forward binding is not convertible. This might be because "
                "converter<Receiver>::to is missing for the receiver, or converter<T>::to "
                "is missing for an argument.");
            Context::backend_t::template invoke<R>(
                ctx, receiver, std::move(args)...);
        }
    };

    template <typename R, typename... Args>
    struct import_callable<callable<no_receiver_t, R(Args...)>> {
        using receiver_t = no_receiver_t;
        using signature_t = R(Args...);

        template <typename Context, typename R_ = R,
            std::enable_if_t<!is_void_v<R_>>* = nullptr>
        static R_ invoke(Context& ctx, Args... args) {
            static_assert(is_context_legal<Context>::value,
                "Your Context must define backend_t and backend_t must derive from backend_base<backend_t>.");
            static_assert(is_forward_invocable<Context, no_receiver_t, Args...>::value,
                "Your forward binding is not convertible. This might be because "
                "converter<T>::to is missing for an argument.");
            static_assert(is_forward_result_convertible<Context, R>::value,
                "Your forward binding result is not convertible. This might be because "
                "converter<R>::from is missing for the backend dynamic value.");
            return Context::backend_t::template invoke<R>(
                ctx, no_receiver_t{}, std::move(args)...);
        }

        template <typename Context, typename R_ = R,
            std::enable_if_t<is_void_v<R_>>* = nullptr>
        static void invoke(Context& ctx, Args... args) {
            static_assert(is_context_legal<Context>::value,
                "Your Context must define backend_t and backend_t must derive from backend_base<backend_t>.");
            static_assert(is_forward_invocable<Context, no_receiver_t, Args...>::value,
                "Your forward binding is not convertible. This might be because "
                "converter<T>::to is missing for an argument.");
            Context::backend_t::template invoke<R>(
                ctx, no_receiver_t{}, std::move(args)...);
        }
    };

    template <typename... Contracts>
    struct import_callable_group;

    template <typename Head, typename... Tail>
    struct import_callable_group<Head, Tail...>
        : import_callable<Head>, import_callable_group<Tail...> {
        static_assert(is_callable_v<Head>, "given type must be callable<Receiver, Signature>");

        using import_callable<Head>::invoke;
        using import_callable_group<Tail...>::invoke;
    };

    template <>
    struct import_callable_group<> {
        static void invoke() noexcept = delete;
    };

    template <typename Contract>
    struct import_constructor;

    template <typename Receiver, typename... Args>
    struct import_constructor<free_callable<Receiver(Args...)>> {
        template <typename Context>
        static Receiver construct(Context& ctx, Args... args) {
            static_assert(is_context_legal<Context>::value,
                "Your Context must define backend_t and backend_t must derive from backend_base<backend_t>.");
            static_assert(is_forward_constructible<Context, Receiver, Args...>::value,
                "Your imported constructor is not constructible. This might be because "
                "the constructor signature is not declared, or "
                "backend_t::object_t<Receiver, import_t>(ctx, construct_object, args...) is missing.");
            using object_t = typename Context::backend_t::template object_t<Receiver, import_t>;
            return Receiver(ctx, object_t(ctx, construct_object, std::move(args)...));
        }
    };

    template <typename... Contracts>
    struct import_constructor_group;

    template <typename Head, typename... Tail>
    struct import_constructor_group<Head, Tail...>
        : import_constructor<Head>, import_constructor_group<Tail...> {
        static_assert(is_callable_v<Head>, "given type must be callable<Receiver, Signature>");

        using import_constructor<Head>::construct;
        using import_constructor_group<Tail...>::construct;
    };

    template <>
    struct import_constructor_group<> {
        static void construct() noexcept = delete;
    };
}

#endif //DYNABRIDGE_IMPORT_CALLABLE_H
