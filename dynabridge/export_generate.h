#include "config.h"
#include "export_core.h"

namespace dynabridge {
#define BEGIN_CALLABLE_GROUP(name) \
    struct export_##name##_callable_group { \
        static const char* symbol_name() noexcept { return #name; } \
        using overloads_t = type_list<
#define DECL_CALLABLE(result, ...) free_callable<result(__VA_ARGS__)>,
#define DECL_FUNCTION(sig) free_callable<sig>,
#define END_CALLABLE_GROUP \
            free_callable<unmatched_callable_t(unmatched_callable_t)> \
        >; \
        using group_t = typename callable_group_from_type_list<overloads_t>::type; \
        template <typename Signature, typename Context, typename Module, typename Callable> \
        static auto bind(Context& ctx, Module& module, Callable&& callable) \
            -> decltype(export_free_callable_impl<Signature>( \
                ctx, module, symbol_name(), std::forward<Callable>(callable))) \
        { \
            using callable_t = typename std::decay<Callable>::type; \
            static_assert( \
                is_declared_free_callable<Signature, group_t>::value, \
                "This callable group does not declare this free callable signature."); \
            static_assert( \
                is_export_callable_bindable<Signature, Context, callable_t>::value, \
                "Your export is not bindable with this Signature."); \
            return export_free_callable_impl<Signature>( \
                ctx, module, symbol_name(), std::forward<Callable>(callable)); \
        } \
    };
#define BEGIN_CLASS(ns, clazz)
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(result, ...)
#define END_MEMBER_CALLABLE_GROUP
#define END_CLASS
    #include DYNABRIDGE_EXPORT_DEF
#undef END_CLASS
#undef END_MEMBER_CALLABLE_GROUP
#undef DECL_MEMBER_FUNCTION
#undef BEGIN_MEMBER_CALLABLE_GROUP
#undef DECL_CONSTRUCTOR
#undef BEGIN_CLASS
#undef END_CALLABLE_GROUP
#undef DECL_FUNCTION
#undef DECL_CALLABLE
#undef BEGIN_CALLABLE_GROUP

#define BEGIN_CALLABLE_GROUP(name) \
    template <typename Context, typename Module> \
    auto export_##name(Context& ctx, Module& module) \
        -> export_free_callable_group_builder<export_##name##_callable_group, Context, Module> \
    { \
        return export_free_callable_group_builder< \
            export_##name##_callable_group, Context, Module>(ctx, module); \
    }
#define DECL_CALLABLE(result, ...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(ns, clazz)
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(result, ...)
#define END_MEMBER_CALLABLE_GROUP
#define END_CLASS
    #include DYNABRIDGE_EXPORT_DEF
#undef END_CLASS
#undef END_MEMBER_CALLABLE_GROUP
#undef DECL_MEMBER_FUNCTION
#undef BEGIN_MEMBER_CALLABLE_GROUP
#undef DECL_CONSTRUCTOR
#undef BEGIN_CLASS
#undef END_CALLABLE_GROUP
#undef DECL_FUNCTION
#undef DECL_CALLABLE
#undef BEGIN_CALLABLE_GROUP

#define BEGIN_CALLABLE_GROUP(name) \
    template <typename Signature, typename Context, typename Module, typename Callable> \
    auto export_##name(Context& ctx, Module& module, Callable&& callable) \
        -> decltype(export_##name##_callable_group::template bind<Signature>( \
            ctx, module, std::forward<Callable>(callable))) \
    { \
        return export_##name##_callable_group::template bind<Signature>( \
            ctx, module, std::forward<Callable>(callable)); \
    }
#define DECL_CALLABLE(result, ...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(ns, clazz)
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(result, ...)
#define END_MEMBER_CALLABLE_GROUP
#define END_CLASS
    #include DYNABRIDGE_EXPORT_DEF
#undef END_CLASS
#undef END_MEMBER_CALLABLE_GROUP
#undef DECL_MEMBER_FUNCTION
#undef BEGIN_MEMBER_CALLABLE_GROUP
#undef DECL_CONSTRUCTOR
#undef BEGIN_CLASS
#undef END_CALLABLE_GROUP
#undef DECL_FUNCTION
#undef DECL_CALLABLE
#undef BEGIN_CALLABLE_GROUP

#define BEGIN_CALLABLE_GROUP(name) \
    template < \
        typename ExplicitSignature = void, \
        typename Context, \
        typename Module, \
        typename R, \
        std::enable_if_t<std::is_same<ExplicitSignature, void>::value>* = nullptr, \
        typename... Args> \
    auto export_##name(Context& ctx, Module& module, R (*function)(Args...)) \
        -> decltype(export_##name##_callable_group::template bind<R(Args...)>(ctx, module, function)) \
    { \
        return export_##name##_callable_group::template bind<R(Args...)>(ctx, module, function); \
    }
#define DECL_CALLABLE(result, ...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(ns, clazz)
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(result, ...)
#define END_MEMBER_CALLABLE_GROUP
#define END_CLASS
    #include DYNABRIDGE_EXPORT_DEF
#undef END_CLASS
#undef END_MEMBER_CALLABLE_GROUP
#undef DECL_MEMBER_FUNCTION
#undef BEGIN_MEMBER_CALLABLE_GROUP
#undef DECL_CONSTRUCTOR
#undef BEGIN_CLASS
#undef END_CALLABLE_GROUP
#undef DECL_FUNCTION
#undef DECL_CALLABLE
#undef BEGIN_CALLABLE_GROUP

    namespace exports {
#define BEGIN_CALLABLE_GROUP(name)
#define DECL_CALLABLE(result, ...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(ns, clazz) \
        template <typename Native> \
        class clazz { \
        public: \
            using class_t = clazz<Native>; \
            using native_t = Native; \
            using bridge_direction = export_t; \
            static const char* symbol_name() noexcept { return #clazz; } \
            template <typename Context, typename Module> \
            static void register_all(Context& ctx, Module& module) \
            { \
                using backend_t = typename Context::backend_t; \
                auto target = backend_t::template define_class<class_t>(ctx, module, symbol_name()); \
                export_class_registration<class_t, Context, decltype(target)> registration(ctx, target); \
                export_class_registrar<class_t>::register_all(registration); \
                backend_t::template store_export_class<class_t>(ctx, std::move(target)); \
            } \
            template < \
                typename... Args, \
                typename = std::enable_if_t< \
                    std::is_constructible<export_native_storage<native_t>, Args&&...>::value>> \
            explicit clazz(Args&&... args) \
                : native_(std::forward<Args>(args)...) { \
            } \
            native_t& native() noexcept { return native_.ref(); } \
            const native_t& native() const noexcept { return native_.ref(); }
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name) \
            template <typename... Args> \
            auto name(Args&&... args) \
                -> decltype(std::declval<native_t&>().name(std::forward<Args>(args)...)) \
            { \
                return native().name(std::forward<Args>(args)...); \
            } \
            template <typename... Args> \
            auto name(Args&&... args) const \
                -> decltype(std::declval<const native_t&>().name(std::forward<Args>(args)...)) \
            { \
                return native().name(std::forward<Args>(args)...); \
            } \
            struct name##_member { \
                using receiver_t = class_t; \
                template <typename... Args> \
                static auto call(receiver_t& receiver, Args&&... args) \
                    -> decltype(receiver.name(std::forward<Args>(args)...)) \
                { \
                    return receiver.name(std::forward<Args>(args)...); \
                } \
            };
#define DECL_MEMBER_FUNCTION(result, ...)
#define END_MEMBER_CALLABLE_GROUP
#define END_CLASS \
        private: \
            export_native_storage<native_t> native_; \
        };
    #include DYNABRIDGE_EXPORT_DEF
#undef END_CLASS
#undef END_MEMBER_CALLABLE_GROUP
#undef DECL_MEMBER_FUNCTION
#undef BEGIN_MEMBER_CALLABLE_GROUP
#undef DECL_CONSTRUCTOR
#undef BEGIN_CLASS
#undef END_CALLABLE_GROUP
#undef DECL_FUNCTION
#undef DECL_CALLABLE
#undef BEGIN_CALLABLE_GROUP
    }

#define BEGIN_CALLABLE_GROUP(name)
#define DECL_CALLABLE(result, ...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(ns, clazz) \
    template <typename Native> \
    struct export_constructor_group_for<exports::clazz<Native>> { \
        using type = callable_group<
#define DECL_CONSTRUCTOR(...) free_callable<void(__VA_ARGS__)>,
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(result, ...)
#define END_MEMBER_CALLABLE_GROUP
#define END_CLASS \
            free_callable<unmatched_callable_t(unmatched_callable_t)> \
        >; \
    };
    #include DYNABRIDGE_EXPORT_DEF
#undef END_CLASS
#undef END_MEMBER_CALLABLE_GROUP
#undef DECL_MEMBER_FUNCTION
#undef BEGIN_MEMBER_CALLABLE_GROUP
#undef DECL_CONSTRUCTOR
#undef BEGIN_CLASS
#undef END_CALLABLE_GROUP
#undef DECL_FUNCTION
#undef DECL_CALLABLE
#undef BEGIN_CALLABLE_GROUP

#define BEGIN_CALLABLE_GROUP(name)
#define DECL_CALLABLE(result, ...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(ns, clazz) \
    template <typename Native> \
    struct export_class_registrar<exports::clazz<Native>> { \
        using class_t = exports::clazz<Native>; \
        template <typename RegisteredClass> \
        static void register_all(RegisteredClass& type) {
#define DECL_CONSTRUCTOR(...) \
            type.template constructor<__VA_ARGS__>();
#define BEGIN_MEMBER_CALLABLE_GROUP(name) \
            { \
                const char* const callable_name = #name; \
                using member_tag = typename class_t::name##_member;
#define DECL_MEMBER_FUNCTION(result, ...) \
                type.template member<result(__VA_ARGS__)>( \
                    callable_name, \
                    export_member_forwarder<member_tag, result(__VA_ARGS__)>{});
#define END_MEMBER_CALLABLE_GROUP \
            }
#define END_CLASS \
        } \
    };
    #include DYNABRIDGE_EXPORT_DEF
#undef END_CLASS
#undef END_MEMBER_CALLABLE_GROUP
#undef DECL_MEMBER_FUNCTION
#undef BEGIN_MEMBER_CALLABLE_GROUP
#undef DECL_CONSTRUCTOR
#undef BEGIN_CLASS
#undef END_CALLABLE_GROUP
#undef DECL_FUNCTION
#undef DECL_CALLABLE
#undef BEGIN_CALLABLE_GROUP
}
