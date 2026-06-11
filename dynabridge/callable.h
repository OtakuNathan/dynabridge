#ifndef DYNABRIDGE_CALLABLE_H
#define DYNABRIDGE_CALLABLE_H

#include <cstddef>
#include <type_traits>

#include "traits.h"

namespace dynabridge {
    template <typename Receiver, typename Signature>
    struct callable;

    template <typename Receiver, typename R, typename... Args>
    struct callable<Receiver, R(Args...)> {
        static_assert(are_bridge_params_valid<Args...>::value,
            "bridge callable arguments cannot be non-const lvalue references; "
            "use value, const&, or && instead.");

        using receiver_t = Receiver;
        using signature_t = R(Args...);
    };

    template <typename Signature>
    using free_callable = callable<no_receiver_t, Signature>;

    template <typename Contract>
    struct callable_arity;

    template <typename Receiver, typename R, typename... Args>
    struct callable_arity<callable<Receiver, R(Args...)>>
        : std::integral_constant<std::size_t, sizeof...(Args)> {
    };

    template <typename ContractList>
    struct max_callable_arity;

    template <>
    struct max_callable_arity<type_list<>>
        : std::integral_constant<std::size_t, 0> {
    };

    template <typename Head, typename... Tail>
    struct max_callable_arity<type_list<Head, Tail...>>
        : std::integral_constant<
            std::size_t,
            (callable_arity<Head>::value > max_callable_arity<type_list<Tail...>>::value
                ? callable_arity<Head>::value
                : max_callable_arity<type_list<Tail...>>::value)> {
    };

    template <typename T>
    struct is_callable : std::false_type {
    };

    template <typename Receiver, typename Signature>
    struct is_callable<callable<Receiver, Signature>> : std::true_type {
    };

    template <typename T>
    constexpr bool is_callable_v = is_callable<T>::value;

    template <typename... Contracts>
    struct callable_group;

    template <typename Head, typename... Tail>
    struct callable_group<Head, Tail...> : Head, callable_group<Tail...> {
        static_assert(is_callable_v<Head>, "given type must be callable<Receiver, Signature>");

        using contracts_t = type_list<Head, Tail...>;
    };

    template <>
    struct callable_group<> {
        using contracts_t = type_list<>;
    };

    template <typename... Contracts>
    using free_callable_group = callable_group<Contracts...>;

    template <typename>
    struct callable_group_from_type_list;

    template <typename... Contracts>
    struct callable_group_from_type_list<type_list<Contracts...>> {
        using type = callable_group<Contracts...>;
    };

    template <typename Signature, typename Group>
    using is_declared_free_callable =
        std::is_base_of<free_callable<Signature>, Group>;

    template <typename Receiver, typename Signature, typename Group>
    using is_declared_member_callable =
        std::is_base_of<callable<Receiver, Signature>, Group>;
}

#endif //DYNABRIDGE_CALLABLE_H
