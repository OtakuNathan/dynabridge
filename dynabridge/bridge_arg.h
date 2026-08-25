#ifndef DYNABRIDGE_BRIDGE_ARG_H
#define DYNABRIDGE_BRIDGE_ARG_H

#include <type_traits>

#include "traits.h"

namespace dynabridge {
    template <typename ContractList, typename Context, typename Callable>
    struct are_export_overloads_bindable;

    template <typename Group, typename Context, typename Callable>
    auto make_export_group_callable(Context& ctx, Callable&& callable);

    template <typename Class, typename Direction>
    struct object_param {
        using class_t = Class;
        using direction_t = Direction;
    };

    template <typename Group, typename Direction>
    struct callable_param {
        using group_t = Group;
        using direction_t = Direction;
    };

    template <typename T>
    struct is_object_param : std::false_type {
    };

    template <typename Class, typename Direction>
    struct is_object_param<object_param<Class, Direction>> : std::true_type {
    };

    template <typename T>
    struct is_callable_param : std::false_type {
    };

    template <typename Group, typename Direction>
    struct is_callable_param<callable_param<Group, Direction>> : std::true_type {
    };

    template <typename T>
    struct is_bridge_descriptor
        : disjunction<
            is_object_param<typename std::decay<T>::type>,
            is_callable_param<typename std::decay<T>::type>> {
    };

    template <typename T>
    struct is_bridge_value
        : negation<is_bridge_descriptor<typename std::decay<T>::type>> {
    };

    template <typename Declared, typename Context, typename = void>
    struct import_argument {
        using parameter_t = Declared;

        template <typename Value>
        static auto lower(Context& ctx, Value&& value)
            -> decltype(to_cast<Declared>(ctx, std::forward<Value>(value)))
        {
            return to_cast<Declared>(ctx, std::forward<Value>(value));
        }
    };

    template <typename Class, typename Context>
    struct import_argument<object_param<Class, import_t>, Context> {
        using delegate_t = typename Class::template delegate_t<Context>;
        using parameter_t = const delegate_t&;

        static auto lower(Context& ctx, const delegate_t& value)
            -> decltype(Context::backend_t::template to_dynamic_object<Class>(ctx, value.object()))
        {
            return Context::backend_t::template to_dynamic_object<Class>(ctx, value.object());
        }
    };

    template <typename Group, typename Context>
    struct import_argument<callable_param<Group, export_t>, Context> {
        template <typename Callable>
        static auto lower(Context& ctx, Callable&& callable)
            -> decltype(make_export_group_callable<Group>(ctx, std::forward<Callable>(callable)))
        {
            return make_export_group_callable<Group>(ctx, std::forward<Callable>(callable));
        }
    };

    template <typename Group, typename Context>
    struct import_callable_argument {
        template <typename Callable>
        import_callable_argument(Callable&&);
    };

    template <typename Declared, typename Context>
    struct import_parameter {
        using type = typename import_argument<Declared, Context>::parameter_t;
    };

    template <typename Group, typename Context>
    struct import_parameter<callable_param<Group, export_t>, Context> {
        using type = import_callable_argument<Group, Context>;
    };

    template <typename Declared, typename Context>
    using import_parameter_t = typename import_parameter<Declared, Context>::type;
}

#endif // DYNABRIDGE_BRIDGE_ARG_H
