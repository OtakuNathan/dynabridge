#ifndef DYNABRIDGE_EXPORT_CALLABLE_H
#define DYNABRIDGE_EXPORT_CALLABLE_H

#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

#include "backend_base.h"
#include "type_list.h"
#include "callable.h"

namespace dynabridge {
    template <typename, typename T>
    struct dependent_type {
        using type = T;
    };

    template <typename Context, typename ArgList, typename = void>
    struct are_export_arguments_convertible : std::true_type {
    };

    template <typename Context, typename... Args>
    struct are_export_arguments_convertible<
        Context,
        type_list<Args...>,
        void_t<backend_dynamic_value_t<typename Context::backend_t>>> {
        template <typename T>
        using dynamic_t = backend_dynamic_value_t<typename T::backend_t>;

        template <typename T, typename = void>
        struct probe : std::false_type {
        };

        template <typename T>
        struct probe<T, void_t<decltype(from_cast<Args>(
                                std::declval<T&>(),
                                std::declval<dynamic_t<T>>()))...>>
            : std::true_type {
        };

        constexpr static bool value = probe<Context>::value;
    };

    template <typename Context, typename Class, typename ArgList, typename = void>
    struct is_export_member_receiver_convertible : std::true_type {
    };

    template <typename Context, typename Class, typename... Args>
    struct is_export_member_receiver_convertible<
        Context,
        Class,
        type_list<Args...>,
        void_t<backend_dynamic_value_t<typename Context::backend_t>>> {
        template <typename T>
        using dynamic_t = backend_dynamic_value_t<typename T::backend_t>;

        template <typename T, typename = void>
        struct probe : std::false_type {
        };

        template <typename T>
        struct probe<T, void_t<
            decltype(T::backend_t::template bind_export_object_impl<Class>(
                std::declval<T&>(),
                std::declval<dynamic_t<T>>()).native(
                    std::declval<T&>())),
            decltype(from_cast<Args>(
                std::declval<T&>(),
                std::declval<dynamic_t<T>>()))...>>
            : std::true_type {
        };

        constexpr static bool value = probe<Context>::value;
    };

    template <typename Receiver, typename Callable, typename... Args>
    auto invoke_export_member_callable_impl(std::false_type,
        Callable& callable, Receiver& receiver, Args&&... args)
        -> decltype(callable(receiver, std::forward<Args>(args)...))
    {
        return callable(receiver, std::forward<Args>(args)...);
    }

    template <typename Receiver, typename Callable, typename... Args>
    auto invoke_export_member_callable_impl(std::true_type,
        Callable& callable, Receiver& receiver, Args&&... args)
        -> decltype((receiver.*callable)(std::forward<Args>(args)...))
    {
        return (receiver.*callable)(std::forward<Args>(args)...);
    }

    template <typename Receiver, typename Callable, typename... Args>
    auto invoke_export_member_callable_impl(std::true_type,
        Callable& callable, Receiver& receiver, Args&&... args)
        -> decltype((receiver.native().*callable)(std::forward<Args>(args)...))
    {
        return (receiver.native().*callable)(std::forward<Args>(args)...);
    }

    template <typename Receiver, typename Callable, typename... Args>
    auto invoke_export_member_callable(Callable& callable, Receiver& receiver, Args&&... args)
        -> decltype(invoke_export_member_callable_impl<Receiver>(
            typename std::is_member_function_pointer<
                typename std::decay<Callable>::type>::type{},
            callable, receiver, std::forward<Args>(args)...))
    {
        return invoke_export_member_callable_impl<Receiver>(
            typename std::is_member_function_pointer<typename std::decay<Callable>::type>::type{},
            callable, receiver, std::forward<Args>(args)...
        );
    }

    template <typename Signature, typename Context, typename Callable>
    struct is_export_callable_bindable;

    template <typename Context, typename Callable, typename R, typename... Args>
    struct is_export_callable_bindable<R(Args...), Context, Callable> {
        template <typename T, typename = void>
        struct probe : std::false_type { };

        template <typename T>
        struct probe<T, void_t<
            decltype(to_cast<R>(
                std::declval<Context&>(),
                std::declval<T&>()(std::declval<Args>()...)))
        >> : std::true_type { };

        constexpr static bool value =
            are_bridge_params_valid<Args...>::value
            && are_export_arguments_convertible<Context, type_list<Args...>>::value
            && probe<Callable>::value;
    };

    template <typename Context, typename Callable, typename... Args>
    struct is_export_callable_bindable<void(Args...), Context, Callable> {
        template <typename T, typename = void>
        struct probe : std::false_type { };

        template <typename T>
        struct probe<T, void_t<
            decltype(std::declval<T&>()(std::declval<Args>()...))
        >> : std::true_type { };

        constexpr static bool value =
            are_bridge_params_valid<Args...>::value
            && are_export_arguments_convertible<Context, type_list<Args...>>::value
            && probe<Callable>::value;
    };

    template <typename Class, typename Signature, typename Context, typename Callable>
    struct is_export_member_callable_bindable;

    template <typename Class, typename Context, typename Callable, typename R, typename... Args>
    struct is_export_member_callable_bindable<Class, R(Args...), Context, Callable> {
        using receiver_t = Class;

        template <typename T, typename = void>
        struct probe : std::false_type { };

        template <typename T>
        struct probe<T, void_t<
            decltype(to_cast<R>(
                std::declval<Context&>(),
                invoke_export_member_callable(std::declval<T&>(), std::declval<receiver_t&>(), std::declval<Args>()...)
            ))
        >> : std::true_type { };

        constexpr static bool value =
            are_bridge_params_valid<Args...>::value
            && is_export_member_receiver_convertible<Context, Class, type_list<Args...>>::value
            && probe<Callable>::value;
    };

    template <typename Class, typename Context, typename Callable, typename ... Args>
    struct is_export_member_callable_bindable<Class, void(Args...), Context, Callable> {
        using receiver_t = Class;

        template <typename T, typename = void>
        struct probe : std::false_type { };

        template <typename T>
        struct probe<T, void_t<
            decltype(invoke_export_member_callable(std::declval<T&>(), std::declval<receiver_t&>(), std::declval<Args>()...))
        >> : std::true_type { };

        constexpr static bool value =
            are_bridge_params_valid<Args...>::value
            && is_export_member_receiver_convertible<Context, Class, type_list<Args...>>::value
            && probe<Callable>::value;
    };

    template <typename Context, typename Class, typename Callable, typename R,
        typename DynamicReceiver, typename DynamicArgList, typename ArgList,
        bool SizesMatch = (type_list_size<DynamicArgList>::value == type_list_size<ArgList>::value)>
    struct is_export_member_invocable {
        template <typename T, std::size_t... Indices>
        static auto test(std::index_sequence<Indices...>) -> decltype(
            Context::backend_t::template bind_export_object_impl<Class>(
                std::declval<Context&>(),
                std::declval<typename dependent_type<T, DynamicReceiver>::type>()).native(
                    std::declval<Context&>()),
            to_cast<R>(
                std::declval<Context&>(),
                invoke_export_member_callable(
                    std::declval<T&>(),
                    *Context::backend_t::template bind_export_object_impl<Class>(
                        std::declval<Context&>(),
                        std::declval<typename dependent_type<T, DynamicReceiver>::type>()).native(
                            std::declval<Context&>()),
                    from_cast<element_at_t<Indices, ArgList>>(
                        std::declval<Context&>(),
                        std::declval<typename dependent_type<T, element_at_t<Indices, DynamicArgList>>::type>())...
                    )
            ),
            std::true_type{}
        );

        template <typename>
        static auto test(...) -> std::false_type;

        constexpr static bool value = decltype(test<Callable>(
            std::make_index_sequence<type_list_size<ArgList>::value>{}))::value;
    };

    template <typename Context, typename Class, typename Callable,
        typename R, typename DynamicReceiver, typename DynamicArgList, typename ArgList>
    struct is_export_member_invocable<Context, Class, Callable, R, DynamicReceiver, DynamicArgList, ArgList, false>
        : std::false_type {};

    template <typename Context, typename Class, typename Callable,
        typename DynamicReceiver, typename DynamicArgList, typename ArgList,
        bool SizesMatch = (type_list_size<DynamicArgList>::value == type_list_size<ArgList>::value)>
    struct is_void_export_member_invocable {
        template <typename T, std::size_t... Indices>
        static auto test(std::index_sequence<Indices...>) -> decltype(
            Context::backend_t::template bind_export_object_impl<Class>(
                std::declval<Context&>(),
                std::declval<typename dependent_type<T, DynamicReceiver>::type>()).native(
                    std::declval<Context&>()),
            invoke_export_member_callable(
                std::declval<T&>(),
                *Context::backend_t::template bind_export_object_impl<Class>(
                    std::declval<Context&>(),
                    std::declval<typename dependent_type<T, DynamicReceiver>::type>()).native(
                        std::declval<Context&>()),
                from_cast<element_at_t<Indices, ArgList>>(
                    std::declval<Context&>(),
                    std::declval<typename dependent_type<T, element_at_t<Indices, DynamicArgList>>::type>()
                )...
            ),
            std::true_type{}
        );

        template <typename>
        static auto test(...) -> std::false_type;

        constexpr static bool value = decltype(
            test<Callable>(std::make_index_sequence<type_list_size<ArgList>::value>{})
        )::value;
    };

    template <typename Context, typename Class, typename Callable,
        typename DynamicReceiver, typename DynamicArgList, typename ArgList>
    struct is_void_export_member_invocable<Context, Class, Callable, DynamicReceiver, DynamicArgList, ArgList, false>
        : std::false_type {};

    template <typename Contract>
    struct export_callable;

    template <typename Contract>
    struct is_unmatched_free_callable : std::false_type {
    };

    template <>
    struct is_unmatched_free_callable<free_callable<unmatched_callable_t(unmatched_callable_t)>>
        : std::true_type {
    };

    template <
        typename Context,
        typename Callable,
        typename R,
        typename DynamicArgList,
        typename ArgList,
        bool SizesMatch =
            (type_list_size<DynamicArgList>::value == type_list_size<ArgList>::value)>
    struct is_export_invocable {
        template <typename T, std::size_t... Indices>
        static auto test(std::index_sequence<Indices...>) -> decltype(
            to_cast<R>(
                std::declval<Context&>(),
                std::declval<T&>()(
                    from_cast<element_at_t<Indices, ArgList>>(
                        std::declval<Context&>(),
                        std::declval<typename dependent_type<T, element_at_t<Indices, DynamicArgList>>::type>()
                    )...
                )
            ),
            std::true_type{}
        );

        template <typename>
        static auto test(...) -> std::false_type;

        constexpr static bool value = decltype(test<Callable>(
            std::make_index_sequence<type_list_size<ArgList>::value>{}))::value;
    };

    template <typename Context, typename Callable, typename R, typename DynamicArgList, typename ArgList>
    struct is_export_invocable<Context, Callable, R, DynamicArgList, ArgList, false>
        : std::false_type {};

    template <typename Context, typename Callable, typename DynamicArgList, typename ArgList,
        bool SizesMatch = (type_list_size<DynamicArgList>::value == type_list_size<ArgList>::value)>
    struct is_void_export_invocable {
        template <typename T, std::size_t... Indices>
        static auto test(std::index_sequence<Indices...>) -> decltype(
            std::declval<T&>()(
                from_cast<element_at_t<Indices, ArgList>>(
                    std::declval<Context&>(),
                    std::declval<typename dependent_type<T, element_at_t<Indices, DynamicArgList>>::type>()
                )...
            ),
            std::true_type{}
        );

        template <typename>
        static auto test(...) -> std::false_type;

        constexpr static bool value = decltype(test<Callable>(
            std::make_index_sequence<type_list_size<ArgList>::value>{}))::value;
    };

    template <typename Context, typename Callable, typename DynamicArgList, typename ArgList>
    struct is_void_export_invocable<Context, Callable, DynamicArgList, ArgList, false>
        : std::false_type {};

    template <typename ContractList, typename Context, typename Callable>
    struct are_export_overloads_bindable;

    template <
        typename Contract,
        typename Context,
        typename Callable,
        bool IsUnmatched = is_unmatched_free_callable<Contract>::value>
    struct is_export_overload_contract_bindable
        : export_callable<Contract>::template is_bindable<
            Context,
            typename std::decay<Callable>::type> {
    };

    template <typename Contract, typename Context, typename Callable>
    struct is_export_overload_contract_bindable<Contract, Context, Callable, true>
        : std::true_type {
    };

    template <typename Context, typename Callable, typename... Contracts>
    struct are_export_overloads_bindable<type_list<Contracts...>, Context, Callable>
        : conjunction<is_export_overload_contract_bindable<Contracts, Context, Callable>...> {
    };

    template <typename Tuple>
    struct tuple_export_arg_accessor {
        constexpr static std::size_t static_arity =
            std::tuple_size<typename std::remove_reference<Tuple>::type>::value;

        Tuple* args = nullptr;

        template <std::size_t I>
        auto get() const -> decltype(std::get<I>(*args)) {
            return std::get<I>(*args);
        }
    };

    template <typename Tuple, std::size_t... Indices>
    bool all_optionals_impl(const Tuple& tuple, std::index_sequence<Indices...>) {
        bool result = true;
        using swallow = int[];
        (void)swallow{0, (result = result && static_cast<bool>(std::get<Indices>(tuple)), 0)...};
        return result;
    }

    template <typename Tuple>
    bool all_optionals(const Tuple& tuple) {
        return all_optionals_impl(
            tuple,
            std::make_index_sequence<std::tuple_size<typename std::remove_reference<Tuple>::type>::value>{});
    }

    template <typename Arg, typename Optional>
    auto optional_arg(Optional& value) -> decltype(std::forward<Arg>(*value)) {
        return std::forward<Arg>(*value);
    }

    template <typename Contract, typename Context, typename Callable>
    struct export_overload_candidate;

    template <typename Context, typename Callable, typename R, typename... Args>
    struct export_overload_candidate<free_callable<R(Args...)>, Context, Callable> {
        using dynamic_value_t = backend_dynamic_value_t<typename Context::backend_t>;

        template <typename Accessor>
        static optional<dynamic_value_t> call_optional(Context& ctx, Callable& callable, Accessor accessor) {
            return call_impl<R>(ctx, callable, accessor, std::index_sequence_for<Args...>{});
        }

    private:
        template <typename R_, typename Accessor, std::size_t... Indices,
            std::enable_if_t<!is_void_v<R_>>* = nullptr>
        static optional<dynamic_value_t> call_impl(
            Context& ctx,
            Callable& callable,
            Accessor accessor,
            std::index_sequence<Indices...>)
        {
            auto converted = std::make_tuple(
                from_optional<Args>(ctx, accessor.template get<Indices>())...);
            if (!all_optionals(converted)) {
                return optional<dynamic_value_t>();
            }
            return optional<dynamic_value_t>(to_cast<R_>(
                ctx,
                callable(optional_arg<Args>(std::get<Indices>(converted))...)));
        }

        template <typename R_, typename Accessor, std::size_t... Indices,
            std::enable_if_t<is_void_v<R_>>* = nullptr>
        static optional<dynamic_value_t> call_impl(
            Context& ctx,
            Callable& callable,
            Accessor accessor,
            std::index_sequence<Indices...>)
        {
            auto converted = std::make_tuple(
                from_optional<Args>(ctx, accessor.template get<Indices>())...);
            if (!all_optionals(converted)) {
                return optional<dynamic_value_t>();
            }
            callable(optional_arg<Args>(std::get<Indices>(converted))...);
            return optional<dynamic_value_t>(Context::backend_t::undefined(ctx));
        }
    };

    template <
        typename ContractList,
        typename Context,
        typename Callable>
    struct export_overload_dispatch;

    template <typename Context, typename Callable>
    struct export_overload_dispatch<type_list<>, Context, Callable> {
        using dynamic_value_t = backend_dynamic_value_t<typename Context::backend_t>;

        template <typename Accessor>
        static optional<dynamic_value_t> call_optional(Context&, Callable&, std::size_t, Accessor) {
            return optional<dynamic_value_t>();
        }
    };

    template <typename Head, typename... Tail, typename Context, typename Callable>
    struct export_overload_dispatch<type_list<Head, Tail...>, Context, Callable> {
        using dynamic_value_t = backend_dynamic_value_t<typename Context::backend_t>;

        template <typename Accessor>
        static optional<dynamic_value_t> call_optional(
            Context& ctx,
            Callable& callable,
            std::size_t argc,
            Accessor accessor)
        {
            return call_impl(
                ctx,
                callable,
                argc,
                accessor,
                typename is_unmatched_free_callable<Head>::type{},
                std::integral_constant<
                    bool,
                    Accessor::static_arity == dynamic_arity
                        || Accessor::static_arity >= callable_arity<Head>::value>{});
        }

    private:
        template <typename Accessor>
        static optional<dynamic_value_t> call_impl(
            Context& ctx,
            Callable& callable,
            std::size_t argc,
            Accessor accessor,
            std::false_type,
            std::true_type)
        {
            if (argc == callable_arity<Head>::value) {
                auto result = export_overload_candidate<Head, Context, Callable>::call_optional(
                    ctx, callable, accessor);
                if (result) {
                    return result;
                }
            }
            return export_overload_dispatch<
                type_list<Tail...>,
                Context,
                Callable>::call_optional(ctx, callable, argc, accessor);
        }

        template <typename Accessor>
        static optional<dynamic_value_t> call_impl(
            Context& ctx,
            Callable& callable,
            std::size_t argc,
            Accessor accessor,
            std::false_type,
            std::false_type)
        {
            return export_overload_dispatch<
                type_list<Tail...>,
                Context,
                Callable>::call_optional(ctx, callable, argc, accessor);
        }

        template <typename Accessor, typename CanAccess>
        static optional<dynamic_value_t> call_impl(
            Context& ctx,
            Callable& callable,
            std::size_t argc,
            Accessor accessor,
            std::true_type,
            CanAccess)
        {
            return export_overload_dispatch<
                type_list<Tail...>,
                Context,
                Callable>::call_optional(ctx, callable, argc, accessor);
        }
    };

    template <
        typename ContractList,
        typename Context,
        typename Callable>
    struct export_overload_required_dispatch {
        using dynamic_value_t = backend_dynamic_value_t<typename Context::backend_t>;

        template <typename Accessor>
        static dynamic_value_t call(Context& ctx, Callable& callable, std::size_t argc, Accessor accessor) {
            auto result = export_overload_dispatch<ContractList, Context, Callable>::call_optional(
                ctx, callable, argc, accessor);
            if (result) {
                return *result;
            }
            throw bad_conversion("dynabridge export overload has no matching signature");
        }
    };

    template <typename Overloads, typename Context, typename Callable>
    struct export_overload_binder {
        static_assert(
            has_backend_dynamic_value<typename Context::backend_t>::value,
            "export overload dispatch requires backend_t::dynamic_value_t.");
        static_assert(
            are_export_overloads_bindable<Overloads, Context, Callable>::value,
            "Your export overload group is not bindable. Every declared overload must be "
            "bindable by the C++ callable.");

        using context_t = Context;
        using callable_t = Callable;
        using overloads_t = Overloads;
        using dynamic_value_t = backend_dynamic_value_t<typename Context::backend_t>;

        export_overload_binder(Context& ctx, Callable callable)
            noexcept(is_nothrow_move_constructible_v<Callable>)
            : ctx_(&ctx), callable_(std::move(callable)) {
        }

        template <typename Accessor>
        optional<dynamic_value_t> dispatch_optional(std::size_t argc, Accessor accessor) {
            return export_overload_dispatch<Overloads, Context, Callable>::call_optional(
                *ctx_, callable_, argc, accessor);
        }

        template <typename Accessor>
        dynamic_value_t dispatch(std::size_t argc, Accessor accessor) {
            return export_overload_required_dispatch<Overloads, Context, Callable>::call(
                *ctx_, callable_, argc, accessor);
        }

        template <typename... DynamicArgs>
        optional<dynamic_value_t> call_optional(DynamicArgs... args) {
            auto tuple = std::make_tuple(args...);
            return export_overload_dispatch<
                Overloads,
                Context,
                Callable>::call_optional(
                    *ctx_,
                    callable_,
                    sizeof...(DynamicArgs),
                    tuple_export_arg_accessor<decltype(tuple)>{&tuple});
        }

        template <typename... DynamicArgs>
        dynamic_value_t operator()(DynamicArgs... args) {
            auto tuple = std::make_tuple(args...);
            return export_overload_required_dispatch<
                Overloads,
                Context,
                Callable>::call(
                    *ctx_,
                    callable_,
                    sizeof...(DynamicArgs),
                    tuple_export_arg_accessor<decltype(tuple)>{&tuple});
        }

    private:
        Context* ctx_;
        Callable callable_;
    };

    template <typename Overloads, typename Context, typename Callable>
    auto create_export_overload_binder(Context& ctx, Callable&& callable)
        -> export_overload_binder<
            Overloads,
            Context,
            typename std::decay<Callable>::type>;

    template <typename Overloads, typename Context, typename Target, typename Callable>
    auto export_free_callable_overloads_impl(Context& ctx, Target& target, const char* name, Callable&& callable)
        -> decltype(Context::backend_t::define(ctx, target, name,
            create_export_overload_binder<Overloads>(ctx, std::forward<Callable>(callable))));

    template <typename Contract, typename Callable>
    struct bound_export_overload_slot;

    template <typename Callable, typename R, typename... Args>
    struct bound_export_overload_slot<free_callable<R(Args...)>, Callable> {
        explicit bound_export_overload_slot(Callable callable)
            noexcept(is_nothrow_move_constructible_v<Callable>)
            : callable_(std::move(callable)) {
        }

        R operator()(Args... args) {
            return callable_(std::forward<Args>(args)...);
        }

        Callable callable_;
    };

    template <typename Callable, typename... Args>
    struct bound_export_overload_slot<free_callable<void(Args...)>, Callable> {
        explicit bound_export_overload_slot(Callable callable)
            noexcept(is_nothrow_move_constructible_v<Callable>)
            : callable_(std::move(callable)) {
        }

        void operator()(Args... args) {
            callable_(std::forward<Args>(args)...);
        }

        Callable callable_;
    };

    template <typename... Slots>
    struct export_bound_overload_set;

    template <>
    struct export_bound_overload_set<> {
        export_bound_overload_set() noexcept = default;

        void operator()() const = delete;
    };

    template <typename Head, typename... Tail>
    struct export_bound_overload_set<Head, Tail...>
        : Head,
          export_bound_overload_set<Tail...> {
        using Head::operator();
        using export_bound_overload_set<Tail...>::operator();

        export_bound_overload_set(Head head, Tail... tail)
            noexcept(
                is_nothrow_move_constructible_v<Head>
                && is_nothrow_move_constructible_v<export_bound_overload_set<Tail...>>)
            : Head(std::move(head)),
              export_bound_overload_set<Tail...>(std::move(tail)...) {
        }
    };

    template <typename Contract, typename... Slots>
    struct has_bound_export_overload_slot : std::false_type {
    };

    template <typename Contract, typename Callable, typename... Tail>
    struct has_bound_export_overload_slot<
        Contract,
        bound_export_overload_slot<Contract, Callable>,
        Tail...> : std::true_type {
    };

    template <typename Contract, typename Head, typename... Tail>
    struct has_bound_export_overload_slot<Contract, Head, Tail...>
        : has_bound_export_overload_slot<Contract, Tail...> {
    };

    template <typename ContractList, typename... Slots>
    struct is_export_overload_builder_complete;

    template <typename... Slots>
    struct is_export_overload_builder_complete<type_list<>, Slots...> : std::true_type {
    };

    template <typename Head, typename... Tail, typename... Slots>
    struct is_export_overload_builder_complete<type_list<Head, Tail...>, Slots...>
        : std::integral_constant<
            bool,
            (is_unmatched_free_callable<Head>::value
                || has_bound_export_overload_slot<Head, Slots...>::value)
            && is_export_overload_builder_complete<type_list<Tail...>, Slots...>::value> {
    };

    template <typename Group, typename Context, typename Target, typename... Slots>
    class export_free_callable_group_builder {
    public:
        using context_t = Context;
        using target_t = Target;
        using overloads_t = typename Group::overloads_t;
        using group_t = typename Group::group_t;
        using slots_t = export_bound_overload_set<Slots...>;

        export_free_callable_group_builder(Context& ctx, Target& target)
            : ctx_(&ctx), target_(&target), slots_() {
        }

        export_free_callable_group_builder(Context* ctx, Target* target, Slots... slots)
            noexcept(is_nothrow_move_constructible_v<slots_t>)
            : ctx_(ctx), target_(target), slots_(std::move(slots)...) {
        }

        template <typename Signature, typename Callable>
        auto bind(Callable&& callable) {
            using contract_t = free_callable<Signature>;
            using callable_t = typename std::decay<Callable>::type;
            using slot_t = bound_export_overload_slot<contract_t, callable_t>;
            using next_builder_t = export_free_callable_group_builder<
                Group, Context, Target, Slots..., slot_t>;

            static_assert(
                is_declared_free_callable<Signature, group_t>::value,
                "This callable group does not declare this free callable signature.");
            static_assert(
                !has_bound_export_overload_slot<contract_t, Slots...>::value,
                "This export callable signature has already been bound.");
            static_assert(
                is_export_callable_bindable<Signature, Context, callable_t>::value,
                "Your export is not bindable with this Signature.");

            return next_builder_t(
                ctx_,
                target_,
                std::move(static_cast<Slots&>(slots_))...,
                slot_t(std::forward<Callable>(callable)));
        }

        auto commit() {
            static_assert(
                is_export_overload_builder_complete<overloads_t, Slots...>::value,
                "Every declared export overload must be bound before commit().");

            return export_free_callable_overloads_impl<overloads_t>(
                *ctx_,
                *target_,
                Group::symbol_name(),
                std::move(slots_));
        }

    private:
        Context* ctx_;
        Target* target_;
        slots_t slots_;
    };

    // callable binder
    template <typename Signature, typename Context, typename Callable>
    struct export_callable_binder;

    template <typename Context, typename Callable, typename R, typename... Args>
    struct export_callable_binder<R(Args...), Context, Callable> {
        static_assert(!is_void_v<R>, "Use the void export_callable_binder specialization.");
        static_assert(are_bridge_params_valid<Args...>::value,
            "bridge callable arguments cannot be non-const lvalue references; "
            "use value, const&, or && instead.");
        static_assert(
            is_export_callable_bindable<R(Args...), Context, Callable>::value,
            "Your export binding is not bindable with this Signature. This might be because "
            "converter<T>::from is missing for the backend dynamic value, the C++ callable is "
            "not invocable with Args, or converter<R>::to is missing for the return value.");

        using context_t = Context;
        using callable_t = Callable;
        using signature_t = R(Args...);

        export_callable_binder(Context& ctx, Callable callable)
            noexcept(is_nothrow_move_constructible_v<Callable>)
            : ctx_(&ctx), callable_(std::move(callable)) {
        }

        template <typename... DynamicArgs,
            std::enable_if_t<sizeof...(DynamicArgs) == sizeof...(Args)>* = nullptr>
        auto operator()(DynamicArgs&&... args)
            -> decltype(to_cast<R>(
                std::declval<Context&>(), std::declval<R>()))
        {
            static_assert(
                is_export_invocable<
                    Context, Callable, R, type_list<DynamicArgs&&...>, type_list<Args...>>::value,
                "Your export binding is not invocable with DynamicArgs. This might be because "
                "converter<T>::from is missing for an argument, converter<R>::to is missing for "
                "the return value, or the C++ callable is not invocable with Args.");

            return to_cast<R>(
                *ctx_,
                callable_(from_cast<Args>(
                    *ctx_, std::forward<DynamicArgs>(args))...));
        }

    private:
        Context* ctx_;
        Callable callable_;
    };

    template <typename Context, typename Callable, typename... Args>
    struct export_callable_binder<void(Args...), Context, Callable> {
        static_assert(are_bridge_params_valid<Args...>::value,
            "bridge callable arguments cannot be non-const lvalue references; "
            "use value, const&, or && instead.");
        static_assert(
            is_export_callable_bindable<void(Args...), Context, Callable>::value,
            "Your export binding is not bindable with this Signature. This might be because "
            "converter<T>::from is missing for the backend dynamic value, or the C++ callable "
            "is not invocable with Args.");

        using context_t = Context;
        using callable_t = Callable;
        using signature_t = void(Args...);

        export_callable_binder(Context& ctx, Callable callable)
            noexcept(is_nothrow_move_constructible_v<Callable>)
            : ctx_(&ctx), callable_(std::move(callable)) {
        }

        template <typename... DynamicArgs,
            std::enable_if_t<sizeof...(DynamicArgs) == sizeof...(Args)>* = nullptr>
        void operator()(DynamicArgs&&... args) {
            static_assert(
                is_void_export_invocable<
                    Context, Callable, type_list<DynamicArgs&&...>, type_list<Args...>>::value,
                "Your export binding is not invocable with DynamicArgs. This might be because "
                "converter<T>::from is missing for an argument, or the C++ callable is not "
                "invocable with Args.");

            callable_(from_cast<Args>(
                *ctx_, std::forward<DynamicArgs>(args))...);
        }

    private:
        Context* ctx_;
        Callable callable_;
    };

    template <typename Class, typename Signature, typename Context, typename Callable>
    struct export_member_callable_binder;

    template <typename Class, typename Context, typename Callable, typename R, typename... Args>
    struct export_member_callable_binder<Class, R(Args...), Context, Callable> {
        static_assert(!is_void_v<R>, "Use the void export_member_callable_binder specialization.");
        static_assert(are_bridge_params_valid<Args...>::value,
            "bridge callable arguments cannot be non-const lvalue references; "
            "use value, const&, or && instead.");
        static_assert(
            is_export_member_callable_bindable<Class, R(Args...), Context, Callable>::value,
            "Your export member binding is not bindable with this Signature. This might be "
            "because the backend cannot bind the dynamic receiver, converter<T>::from is "
            "missing for the backend dynamic value, the C++ callable is not invocable with the "
            "generated proxy receiver and Args, or converter<R>::to is missing for the return value.");

        using context_t = Context;
        using class_t = Class;
        using receiver_t = Class;
        using callable_t = Callable;
        using signature_t = R(Class, Args...);

        export_member_callable_binder(Context& ctx, Callable callable)
            noexcept(is_nothrow_move_constructible_v<Callable>)
            : ctx_(&ctx), callable_(std::move(callable)) {
        }

        template <typename DynamicReceiver, typename... DynamicArgs,
            std::enable_if_t<sizeof...(DynamicArgs) == sizeof...(Args)>* = nullptr>
        auto operator()(DynamicReceiver&& receiver_arg, DynamicArgs&&... args)
            -> decltype(to_cast<R>(
                std::declval<Context&>(), std::declval<R>()))
        {
            static_assert(is_export_member_invocable<Context, Class, Callable,
                    R, DynamicReceiver&&, type_list<DynamicArgs&&...>,
                    type_list<Args...>>::value,
                "Your export member binding is not invocable with DynamicArgs. This might be "
                "because backend_t::bind_export_object_impl<Class>(ctx, self) is missing, "
                "object.native(ctx) cannot unwrap the generated proxy receiver, converter<T>::from is "
                "missing for an argument, converter<R>::to is missing for the return value, "
                "or the C++ callable is not invocable with the generated proxy receiver and Args.");

            auto object = Context::backend_t::template bind_export_object<Class>(
                *ctx_, std::forward<DynamicReceiver>(receiver_arg));
            receiver_t* receiver = object.native(*ctx_);
            if (receiver == nullptr) {
                throw std::runtime_error("dynabridge export object has no proxy receiver");
            }

            return to_cast<R>(*ctx_,
                invoke_export_member_callable(callable_, *receiver,
                    from_cast<Args>(*ctx_, std::forward<DynamicArgs>(args))...
                ));
        }

    private:
        Context* ctx_;
        Callable callable_;
    };

    template <typename Class, typename Context, typename Callable, typename... Args>
    struct export_member_callable_binder<Class, void(Args...), Context, Callable> {
        static_assert(are_bridge_params_valid<Args...>::value,
            "bridge callable arguments cannot be non-const lvalue references; "
            "use value, const&, or && instead.");
        static_assert(
            is_export_member_callable_bindable<Class, void(Args...), Context, Callable>::value,
            "Your export member binding is not bindable with this Signature. This might be "
            "because the backend cannot bind the dynamic receiver, converter<T>::from is "
            "missing for the backend dynamic value, or the C++ callable is not invocable with "
            "the generated proxy receiver and Args.");

        using context_t = Context;
        using class_t = Class;
        using receiver_t = Class;
        using callable_t = Callable;
        using signature_t = void(Class, Args...);

        export_member_callable_binder(Context& ctx, Callable callable)
            noexcept(is_nothrow_move_constructible_v<Callable>)
            : ctx_(&ctx), callable_(std::move(callable)) {
        }

        template <typename DynamicReceiver, typename... DynamicArgs,
            std::enable_if_t<sizeof...(DynamicArgs) == sizeof...(Args)>* = nullptr>
        void operator()(DynamicReceiver&& receiver_arg, DynamicArgs&&... args) {
            static_assert(
                is_void_export_member_invocable<
                    Context,
                    Class,
                    Callable,
                    DynamicReceiver&&,
                    type_list<DynamicArgs&&...>,
                    type_list<Args...>>::value,
                "Your export member binding is not invocable with DynamicArgs. This might be "
                "because backend_t::bind_export_object_impl<Class>(ctx, self) is missing, "
                "object.native(ctx) cannot unwrap the generated proxy receiver, converter<T>::from is "
                "missing for an argument, or the C++ callable is not invocable with the native "
                "proxy receiver and Args.");

            auto object = Context::backend_t::template bind_export_object<Class>(
                *ctx_, std::forward<DynamicReceiver>(receiver_arg));
            receiver_t* receiver = object.native(*ctx_);
            if (receiver == nullptr) {
                throw std::runtime_error("dynabridge export object has no proxy receiver");
            }

            invoke_export_member_callable(callable_, *receiver,
                from_cast<Args>(*ctx_, std::forward<DynamicArgs>(args))...
            );
        }

    private:
        Context* ctx_;
        Callable callable_;
    };

    template <typename Signature, typename Context, typename Callable>
    auto create_export_callable_binder(Context& ctx, Callable&& callable) {
        return export_callable_binder<Signature, Context,
            typename std::decay<Callable>::type>(ctx, std::forward<Callable>(callable));
    }

    template <typename Receiver, typename Signature, typename Context, typename Callable>
    auto create_export_member_callable_binder(Context& ctx, Callable&& callable) {
        return export_member_callable_binder<Receiver, Signature, Context,
            typename std::decay<Callable>::type>(ctx, std::forward<Callable>(callable));
    }

    template <typename Overloads, typename Context, typename Callable>
    auto create_export_overload_binder(Context& ctx, Callable&& callable)
        -> export_overload_binder<
            Overloads,
            Context,
            typename std::decay<Callable>::type>
    {
        return export_overload_binder<Overloads, Context,
            typename std::decay<Callable>::type>(ctx, std::forward<Callable>(callable));
    }

    template <typename Signature>
    struct export_callable<free_callable<Signature>> {
        using contract_t = free_callable<Signature>;

        template <typename Context, typename Callable>
        using is_bindable =
            is_export_callable_bindable<Signature, Context, typename std::decay<Callable>::type>;

        template <typename Context, typename Callable>
        static auto bind(Context& ctx, Callable&& callable)
            -> decltype(create_export_callable_binder<Signature>(
                ctx, std::forward<Callable>(callable)))
        {
            return create_export_callable_binder<Signature>(
                ctx, std::forward<Callable>(callable)
            );
        }
    };

    template <typename Receiver, typename Signature>
    struct export_callable<callable<Receiver, Signature>> {
        using contract_t = callable<Receiver, Signature>;

        template <typename Context, typename Callable>
        using is_bindable = is_export_member_callable_bindable<
            Receiver, Signature, Context,
            typename std::decay<Callable>::type>;

        template <typename Context, typename Callable>
        static auto bind(Context& ctx, Callable&& callable)
            -> decltype(create_export_member_callable_binder<Receiver, Signature>(
                ctx, std::forward<Callable>(callable)))
        {
            return create_export_member_callable_binder<Receiver, Signature>(
                ctx, std::forward<Callable>(callable));
        }
    };

    template <typename Signature, typename Context, typename Target, typename Callable>
    auto export_free_callable_impl(Context& ctx, Target& target, const char* name, Callable&& callable)
        -> decltype(Context::backend_t::define(ctx, target, name,
            export_callable<free_callable<Signature>>::bind(ctx, std::forward<Callable>(callable))))
    {
        static_assert (
            export_callable<free_callable<Signature>>::template is_bindable<Context, Callable>::value,
            "Your export is not bindable with this Signature."
        );

        return Context::backend_t::define(ctx, target, name,
            export_callable<free_callable<Signature>>::bind(ctx, std::forward<Callable>(callable))
        );
    }

    template <typename Signature, typename Context, typename Target, typename Callable>
    auto export_free_callable(Context& ctx, Target& target, const char* name, Callable&& callable)
        -> decltype(export_free_callable_impl<Signature>(
            ctx, target, name, std::forward<Callable>(callable)))
    {
        return export_free_callable_impl<Signature>(
            ctx, target, name, std::forward<Callable>(callable));
    }

    template <
        typename ExplicitSignature = void,
        typename Context,
        typename Target,
        typename R,
        std::enable_if_t<std::is_same<ExplicitSignature, void>::value>* = nullptr,
        typename... Args>
    auto export_free_callable(Context& ctx, Target& target, const char* name, R (*function)(Args...))
        -> decltype(export_free_callable_impl<R(Args...)>(ctx, target, name, function))
    {
        return export_free_callable_impl<R(Args...)>(ctx, target, name, function);
    }

    template <typename Overloads, typename Context, typename Target, typename Callable>
    auto export_free_callable_overloads_impl(Context& ctx, Target& target, const char* name, Callable&& callable)
        -> decltype(Context::backend_t::define(ctx, target, name,
            create_export_overload_binder<Overloads>(ctx, std::forward<Callable>(callable))))
    {
        return Context::backend_t::define(ctx, target, name,
            create_export_overload_binder<Overloads>(ctx, std::forward<Callable>(callable)));
    }

    template <typename Receiver, typename Signature, typename Context, typename Target, typename Fn>
    auto export_member_callable(Context& ctx, Target& target, const char* name, Fn&& fn)
        -> decltype(Context::backend_t::define(ctx, target, name,
            export_callable<callable<Receiver, Signature>>::bind(ctx, std::forward<Fn>(fn))))
    {
        static_assert(
            export_callable<callable<Receiver, Signature>>::template is_bindable<Context, Fn>::value,
            "Your export member is not bindable with this Signature.");

        return Context::backend_t::define(ctx, target, name,
            export_callable<callable<Receiver, Signature>>::bind(ctx, std::forward<Fn>(fn))
        );
    }

}

#endif //DYNABRIDGE_EXPORT_CALLABLE_H
