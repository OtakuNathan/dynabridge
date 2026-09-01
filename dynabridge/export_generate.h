#include "config.h"
#include "export_core.h"

namespace dynabridge {
#define BEGIN_INTERFACE(name)
#define END_INTERFACE
#define IMPLEMENTS(name)

    namespace interfaces {
#undef BEGIN_INTERFACE
#undef END_INTERFACE
#define BEGIN_INTERFACE(name) template <typename Host, typename Direction> class name;
#define END_INTERFACE
#define BEGIN_CALLABLE_GROUP(name)
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(ns, clazz)
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(...)
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
#undef END_INTERFACE
#undef BEGIN_INTERFACE
#define BEGIN_INTERFACE(name)
#define END_INTERFACE
    }

    namespace interface_descriptors {
#undef BEGIN_INTERFACE
#undef END_INTERFACE
#define BEGIN_INTERFACE(name) template <typename Direction> struct name;
#define END_INTERFACE
#define BEGIN_CALLABLE_GROUP(name)
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(ns, clazz)
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(...)
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
#undef END_INTERFACE
#undef BEGIN_INTERFACE
#define BEGIN_INTERFACE(name)
#define END_INTERFACE
    }

    namespace interface_descriptors {
#undef BEGIN_INTERFACE
#undef END_INTERFACE
#define BEGIN_INTERFACE(name) \
        template <> \
        struct name<export_t> { \
            using method_symbols_t = name; \
            static constexpr std::size_t begin_index = DYNABRIDGE_METHOD_INDEX; \
            template <typename Host> \
            using mixin_t = interfaces::name<Host, export_t>;
#define END_INTERFACE \
            static constexpr std::size_t end_index = DYNABRIDGE_METHOD_INDEX; \
        };
#define BEGIN_CALLABLE_GROUP(name)
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(ns, clazz) \
        struct ignored_export_class_##clazz {
#define IMPLEMENTS(name)
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name) \
            struct name; \
            static name method_name_at(method_index<DYNABRIDGE_METHOD_INDEX>); \
            struct name { \
                static constexpr const char* symbol_name() noexcept { return #name; }
#define DECL_MEMBER_FUNCTION(...)
#define END_MEMBER_CALLABLE_GROUP };
#define END_CLASS };
    #include DYNABRIDGE_EXPORT_DEF
#undef END_CLASS
#undef END_MEMBER_CALLABLE_GROUP
#undef DECL_MEMBER_FUNCTION
#undef BEGIN_MEMBER_CALLABLE_GROUP
#undef DECL_CONSTRUCTOR
#undef IMPLEMENTS
#undef BEGIN_CLASS
#undef END_CALLABLE_GROUP
#undef DECL_FUNCTION
#undef DECL_CALLABLE
#undef BEGIN_CALLABLE_GROUP
#undef END_INTERFACE
#undef BEGIN_INTERFACE
#define BEGIN_INTERFACE(name)
#define END_INTERFACE
#define IMPLEMENTS(name)
    }

// Native export types are deliberately completed by user code after including
// bridge.h. Forward declarations keep the generated proxy lazy until use.
#define BEGIN_CALLABLE_GROUP(name)
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(ns, clazz) namespace ns { class clazz; }
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(...)
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

#undef BEGIN_INTERFACE
#undef END_INTERFACE
#define BEGIN_INTERFACE(name)
#define END_INTERFACE
    namespace export_classes {
#define BEGIN_CALLABLE_GROUP(name)
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(ns, clazz) struct clazz;
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(...)
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
    }

#undef IMPLEMENTS
#define BEGIN_CALLABLE_GROUP(name)
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(ns, clazz) \
    template <> \
    struct class_interfaces<export_classes::clazz> { \
        using type = type_list<
#define IMPLEMENTS(name) interface_descriptors::name<export_t>,
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(...)
#define END_MEMBER_CALLABLE_GROUP
#define END_CLASS void>; };
    #include DYNABRIDGE_EXPORT_DEF
#undef END_CLASS
#undef END_MEMBER_CALLABLE_GROUP
#undef DECL_MEMBER_FUNCTION
#undef BEGIN_MEMBER_CALLABLE_GROUP
#undef DECL_CONSTRUCTOR
#undef IMPLEMENTS
#undef BEGIN_CLASS
#undef END_CALLABLE_GROUP
#undef DECL_FUNCTION
#undef DECL_CALLABLE
#undef BEGIN_CALLABLE_GROUP
#define IMPLEMENTS(name)

#undef BEGIN_INTERFACE
#undef END_INTERFACE
#define BEGIN_INTERFACE(name) struct ignored_interface_##name {
#define END_INTERFACE };
#define BEGIN_CALLABLE_GROUP(name)
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(ns, clazz) \
    template <> \
    struct class_method_names<export_classes::clazz> { \
        struct metadata { \
            static constexpr std::size_t begin_index = DYNABRIDGE_METHOD_INDEX;
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name) \
            struct name; \
            static name method_name_at(method_index<DYNABRIDGE_METHOD_INDEX>); \
            struct name { \
                static constexpr const char* symbol_name() noexcept { return #name; } \
            };
#define DECL_MEMBER_FUNCTION(...)
#define END_MEMBER_CALLABLE_GROUP
#define END_CLASS \
            static constexpr std::size_t end_index = DYNABRIDGE_METHOD_INDEX; \
        }; \
        using type = method_names_in_range_t< \
            metadata, metadata::begin_index, metadata::end_index>; \
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
#undef END_INTERFACE
#undef BEGIN_INTERFACE
#define BEGIN_INTERFACE(name)
#define END_INTERFACE

#define OBJECT(clazz) object_param<export_classes::clazz, export_t>
#define CALLABLE(name) callable_param<import_symbols::name, import_t>

    namespace export_groups {
#define BEGIN_CALLABLE_GROUP(name) \
    struct name { \
        static const char* symbol_name() noexcept { return #name; } \
        using overloads_t = type_list<
#define DECL_CALLABLE(...) free_callable<signature_from_types_t<__VA_ARGS__>>,
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
#define DECL_MEMBER_FUNCTION(...)
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
    }

#define BEGIN_CALLABLE_GROUP(name) \
    inline export_callable_builder<export_groups::name> bind_##name() { \
        return export_callable_builder<export_groups::name>(); \
    }
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(ns, clazz)
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(...)
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
        -> export_free_callable_group_builder<export_groups::name, Context, Module> \
    { \
        return export_free_callable_group_builder< \
            export_groups::name, Context, Module>(ctx, module); \
    }
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(ns, clazz)
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(...)
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
        -> decltype(export_groups::name::template bind<Signature>( \
            ctx, module, std::forward<Callable>(callable))) \
    { \
        return export_groups::name::template bind<Signature>( \
            ctx, module, std::forward<Callable>(callable)); \
    }
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(ns, clazz)
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(...)
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
        -> decltype(export_groups::name::template bind<R(Args...)>(ctx, module, function)) \
    { \
        return export_groups::name::template bind<R(Args...)>(ctx, module, function); \
    }
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(ns, clazz)
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(...)
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

    namespace interfaces {
#undef BEGIN_INTERFACE
#undef END_INTERFACE
#define BEGIN_INTERFACE(name) \
        template <typename Host> \
        class name<Host, export_t> { \
        public: \
            using receiver_t = Host; \
            Host& receiver() noexcept { return static_cast<Host&>(*this); }
#define END_INTERFACE };
#define BEGIN_CALLABLE_GROUP(name)
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(ns, clazz) \
        template <typename Host> \
        class ignored_export_class_##clazz { \
        public: \
            Host& receiver() noexcept;
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name) \
            template <typename... Args> \
            decltype(auto) name(Args&&... args) \
            { \
                return receiver().native().name(std::forward<Args>(args)...); \
            } \
            template <typename... Args> \
            decltype(auto) name(Args&&... args) const \
            { \
                return static_cast<const Host&>(*this).native().name( \
                    std::forward<Args>(args)...); \
            } \
            struct name##_member { \
                using receiver_t = Host; \
                template <typename... Args> \
                static decltype(auto) call(receiver_t& receiver, Args&&... args) \
                { \
                    return receiver.name(std::forward<Args>(args)...); \
                } \
            };
#define DECL_MEMBER_FUNCTION(...)
#define END_MEMBER_CALLABLE_GROUP
#define END_CLASS };
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
#undef END_INTERFACE
#undef BEGIN_INTERFACE
#define BEGIN_INTERFACE(name)
#define END_INTERFACE
    }

    namespace exports {
#undef BEGIN_INTERFACE
#undef END_INTERFACE
#define BEGIN_INTERFACE(name) \
        template <typename Native> \
        class ignored_interface_##name { \
        public: \
            using native_t = Native; \
            using class_t = ignored_interface_##name<Native>; \
            native_t& native();
#define END_INTERFACE };
#define BEGIN_CALLABLE_GROUP(name)
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(ns, clazz) \
        template <typename Native> \
        class clazz##_proxy; \
        using clazz = clazz##_proxy<ns::clazz>; \
        template <typename Native> \
        class clazz##_proxy \
            : public interface_pack< \
                clazz##_proxy<Native>, \
                typename class_interfaces<export_classes::clazz>::type, \
                typename class_method_names<export_classes::clazz>::type> { \
        public: \
            using class_t = clazz##_proxy<Native>; \
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
            explicit clazz##_proxy(Args&&... args) \
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
#define DECL_MEMBER_FUNCTION(...)
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
#undef END_INTERFACE
#undef BEGIN_INTERFACE
#define BEGIN_INTERFACE(name)
#define END_INTERFACE
    }

#define BEGIN_CALLABLE_GROUP(name)
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(ns, clazz) \
    template <typename Native> \
    struct export_constructor_group_for<exports::clazz##_proxy<Native>> { \
        using type = callable_group<
#define DECL_CONSTRUCTOR(...) free_callable<void(__VA_ARGS__)>,
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(...)
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

#undef BEGIN_INTERFACE
#undef END_INTERFACE
#define BEGIN_INTERFACE(name) \
    template <typename Host> \
    struct export_interface_registration< \
        interface_descriptors::name<export_t>, Host> { \
        template <typename RegisteredClass> \
        static void register_all(RegisteredClass& type) { \
            static_cast<void>(type);
#define END_INTERFACE \
        } \
    };
#define BEGIN_CALLABLE_GROUP(name)
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(ns, clazz) \
    template <typename Host> \
    struct ignored_interface_registration_##clazz { \
        template <typename RegisteredClass> \
        static void register_all(RegisteredClass& type) { \
            static_cast<void>(type);
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name) \
            { \
                const char* const callable_name = #name; \
                using member_tag = typename Host::name##_member;
#define DECL_MEMBER_FUNCTION(...) \
                type.template member<signature_from_types_t<__VA_ARGS__>>( \
                    callable_name, \
                    export_member_forwarder< \
                        member_tag, signature_from_types_t<__VA_ARGS__>>{});
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
#undef END_INTERFACE
#undef BEGIN_INTERFACE
#define BEGIN_INTERFACE(name) \
    template <typename Class> \
    struct ignored_export_class_registrar_interface_##name { \
        using class_t = Class; \
        template <typename RegisteredClass> \
        static void register_all(RegisteredClass& type) {
#define END_INTERFACE \
        } \
    };

#define BEGIN_CALLABLE_GROUP(name)
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(ns, clazz) \
    template <typename Native> \
    struct export_class_registrar<exports::clazz##_proxy<Native>> { \
        using class_t = exports::clazz##_proxy<Native>; \
        template <typename RegisteredClass> \
        static void register_all(RegisteredClass& type) { \
            export_interface_registrar< \
                class_t, \
                typename class_interfaces<export_classes::clazz>::type \
            >::register_all(type);
#define DECL_CONSTRUCTOR(...) \
            type.template constructor<__VA_ARGS__>();
#define BEGIN_MEMBER_CALLABLE_GROUP(name) \
            { \
                const char* const callable_name = #name; \
                using member_tag = typename class_t::name##_member;
#define DECL_MEMBER_FUNCTION(...) \
                type.template member<signature_from_types_t<__VA_ARGS__>>( \
                    callable_name, \
                    export_member_forwarder< \
                        member_tag, signature_from_types_t<__VA_ARGS__>>{});
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

#undef BEGIN_INTERFACE
#undef END_INTERFACE
#define BEGIN_INTERFACE(name)
#define END_INTERFACE
    namespace export_classes {
#define BEGIN_CALLABLE_GROUP(name)
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(ns, clazz) \
        struct clazz { \
            using native_t = ns::clazz; \
            using proxy_t = exports::clazz; \
            static const char* symbol_name() noexcept { return #clazz; } \
        };
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(...)
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
    }

#undef END_INTERFACE
#undef BEGIN_INTERFACE
#undef IMPLEMENTS
#undef CALLABLE
#undef OBJECT
}
