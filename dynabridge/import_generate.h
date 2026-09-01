#include "config.h"
#include "import_core.h"

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
#define BEGIN_CLASS(clazz)
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(...)
#define END_MEMBER_CALLABLE_GROUP
#define END_CLASS
    #include DYNABRIDGE_IMPORT_DEF
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
#define IMPLEMENTS(name)
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
#define BEGIN_CLASS(clazz)
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(...)
#define END_MEMBER_CALLABLE_GROUP
#define END_CLASS
    #include DYNABRIDGE_IMPORT_DEF
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
#define IMPLEMENTS(name)
    }

    namespace export_groups {
#define BEGIN_CALLABLE_GROUP(name) struct name;
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
    }

    namespace import_symbols {
#define BEGIN_CALLABLE_GROUP(name)
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(clazz) struct clazz;
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(...)
#define END_MEMBER_CALLABLE_GROUP
#define END_CLASS
    #include DYNABRIDGE_IMPORT_DEF
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

#define OBJECT(clazz) object_param<import_symbols::clazz, import_t>
#define CALLABLE(name) callable_param<export_groups::name, export_t>

    namespace interface_descriptors {
#undef BEGIN_INTERFACE
#undef END_INTERFACE
#define BEGIN_INTERFACE(name) \
        template <> \
        struct name<import_t> { \
            template <typename Host> \
            using mixin_t = interfaces::name<Host, import_t>; \
            template <typename ReceiverSymbol> \
            struct symbols { \
                static constexpr std::size_t begin_index = DYNABRIDGE_METHOD_INDEX;
#define END_INTERFACE \
                static constexpr std::size_t end_index = DYNABRIDGE_METHOD_INDEX; \
            }; \
            template <typename ReceiverSymbol> \
            using symbols_t = symbols<ReceiverSymbol>; \
            using method_symbols_t = symbols<void>; \
        };
#define BEGIN_CALLABLE_GROUP(name)
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(clazz) struct ignored_class_##clazz { using ReceiverSymbol = void;
#define IMPLEMENTS(name)
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name) \
            struct name; \
            static name method_name_at(method_index<DYNABRIDGE_METHOD_INDEX>); \
            struct name : interface_receiver_metadata<ReceiverSymbol> { \
                static constexpr const char* symbol_name() noexcept { return #name; } \
                using overloads_t = type_list<
#define DECL_MEMBER_FUNCTION(...) free_callable<signature_from_types_t<__VA_ARGS__>>,
#define END_MEMBER_CALLABLE_GROUP \
                    free_callable<unmatched_callable_t(unmatched_callable_t)> \
                >; \
            };
#define END_CLASS };
    #include DYNABRIDGE_IMPORT_DEF
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

#undef CALLABLE
#undef OBJECT

    namespace export_groups {
#define BEGIN_CALLABLE_GROUP(name) struct name;
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
    }

#define BEGIN_CALLABLE_GROUP(name)
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(clazz) template <typename Context> class clazz;
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(...)
#define END_MEMBER_CALLABLE_GROUP
#define END_CLASS
    #include DYNABRIDGE_IMPORT_DEF
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

    namespace import_symbols {
#define BEGIN_CALLABLE_GROUP(name)
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(clazz) struct clazz;
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(...)
#define END_MEMBER_CALLABLE_GROUP
#define END_CLASS
    #include DYNABRIDGE_IMPORT_DEF
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
#define BEGIN_CLASS(clazz) \
    template <> \
    struct class_interfaces<import_symbols::clazz> { \
        using type = type_list<
#define IMPLEMENTS(name) interface_descriptors::name<import_t>,
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(...)
#define END_MEMBER_CALLABLE_GROUP
#define END_CLASS void>; };
    #include DYNABRIDGE_IMPORT_DEF
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

#define OBJECT(clazz) object_param<import_symbols::clazz, import_t>
#define CALLABLE(name) callable_param<export_groups::name, export_t>

    namespace import_symbols {
#undef BEGIN_INTERFACE
#undef END_INTERFACE
#define BEGIN_INTERFACE(name) struct ignored_interface_##name { using self_symbol_t = void;
#define END_INTERFACE };
#define BEGIN_CALLABLE_GROUP(name) \
    struct name { \
        static constexpr const char* symbol_name() noexcept { return #name; } \
        using overloads_t = type_list<
#define DECL_CALLABLE(...) free_callable<signature_from_types_t<__VA_ARGS__>>,
#define DECL_FUNCTION(sig) free_callable<sig>,
#define END_CALLABLE_GROUP \
            free_callable<unmatched_callable_t(unmatched_callable_t)> \
        >; \
        using group_t = typename callable_group_from_type_list<overloads_t>::type; \
    };
#define BEGIN_CLASS(clazz) \
    struct clazz \
        : interface_symbol_pack<clazz, typename class_interfaces<clazz>::type> { \
        static constexpr std::size_t begin_index = DYNABRIDGE_METHOD_INDEX; \
        using self_symbol_t = clazz; \
        template <typename Context> \
        using delegate_t = dynabridge::clazz<Context>; \
        static constexpr const char* symbol_name() noexcept { return #clazz; }
#define IMPLEMENTS(name)
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name) \
        struct name; \
        static name method_name_at(method_index<DYNABRIDGE_METHOD_INDEX>); \
        struct name { \
            using receiver_symbol_t = self_symbol_t; \
            static constexpr const char* symbol_name() noexcept { return #name; } \
            using overloads_t = type_list<
#define DECL_MEMBER_FUNCTION(...) free_callable<signature_from_types_t<__VA_ARGS__>>,
#define END_MEMBER_CALLABLE_GROUP \
                free_callable<unmatched_callable_t(unmatched_callable_t)> \
            >; \
        };
#define END_CLASS \
        static constexpr std::size_t end_index = DYNABRIDGE_METHOD_INDEX; \
    };
    #include DYNABRIDGE_IMPORT_DEF
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

    template <typename Symbol>
    struct import_constructor_metadata;

#undef BEGIN_INTERFACE
#undef END_INTERFACE
#define BEGIN_INTERFACE(name)
#define END_INTERFACE
#define BEGIN_CALLABLE_GROUP(name)
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(clazz) \
    template <> \
    struct import_constructor_metadata<import_symbols::clazz> { \
        using overloads_t = type_list<
#define DECL_CONSTRUCTOR(...) free_callable<void(__VA_ARGS__)>,
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(...)
#define END_MEMBER_CALLABLE_GROUP
#define END_CLASS \
            free_callable<unmatched_callable_t(unmatched_callable_t)> \
        >; \
    };
    #include DYNABRIDGE_IMPORT_DEF
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
    template <typename Context, typename... Args> \
    decltype(auto) call_##name(Context& ctx, Args&&... args) { \
        using overloads_t = typename import_symbols::name::overloads_t;
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP \
        return import_overload_dispatch<overloads_t>::invoke( \
            ctx, std::forward<Args>(args)...); \
    }
#define BEGIN_CLASS(clazz)
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(...)
#define END_MEMBER_CALLABLE_GROUP
#define END_CLASS
    #include DYNABRIDGE_IMPORT_DEF
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
    template <typename Context> \
    auto name(Context& ctx) { \
        /* CONTRACT (lifetime): the returned callable captures ctx by REFERENCE. \
           It must not outlive ctx — storing it beyond ctx's scope, returning it \
           up the stack, or passing it to another thread is undefined behavior. \
           Use call_##name(ctx, args...) directly when the callable must escape \
           ctx's lifetime. */ \
        return [&ctx](auto&&... args) -> decltype(auto) { \
            return call_##name(ctx, std::forward<decltype(args)>(args)...); \
        }; \
    }
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(clazz)
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(...)
#define END_MEMBER_CALLABLE_GROUP
#define END_CLASS
    #include DYNABRIDGE_IMPORT_DEF
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
#define BEGIN_CALLABLE_GROUP(name)
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(clazz) \
    template <typename Context, typename... Args> \
    auto construct_##clazz(Context& ctx, Args&&... args) { \
        using receiver_t = clazz<Context>; \
        using metadata_t = import_constructor_metadata<import_symbols::clazz>; \
        using overloads_t = typename metadata_t::overloads_t;
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(...)
#define END_MEMBER_CALLABLE_GROUP
#define END_CLASS \
        return import_constructor_overload_dispatch<overloads_t>::template construct<receiver_t>( \
            ctx, std::forward<Args>(args)...); \
    }
    #include DYNABRIDGE_IMPORT_DEF
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
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(clazz) \
    template <> \
    struct import_class_traits<clazz> { \
        template <typename Context, typename... Args> \
        static auto construct(Context& ctx, Args&&... args) { \
            return construct_##clazz(ctx, std::forward<Args>(args)...); \
        } \
    };
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(...)
#define END_MEMBER_CALLABLE_GROUP
#define END_CLASS
    #include DYNABRIDGE_IMPORT_DEF
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
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(clazz) \
    template <typename Context> \
    struct import_receiver_symbol<clazz<Context>> { \
        using type = import_symbols::clazz; \
    };
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(...)
#define END_MEMBER_CALLABLE_GROUP
#define END_CLASS
    #include DYNABRIDGE_IMPORT_DEF
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
    namespace interfaces { \
    template <typename Host> \
    class name<Host, import_t> { \
    public: \
        using receiver_t = Host; \
        using receiver_symbol_t = typename import_receiver_symbol<Host>::type; \
        using descriptor_t = interface_descriptors::name<import_t>; \
        using bridge_symbol_t = typename descriptor_t::template symbols_t<receiver_symbol_t>; \
        auto& context() noexcept { return static_cast<Host&>(*this).context(); } \
        Host& receiver() noexcept { return static_cast<Host&>(*this); }
#define END_INTERFACE \
    }; \
    }
#define BEGIN_CALLABLE_GROUP(name)
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(clazz) \
    template <typename Context> \
    class clazz \
        : public interface_pack< \
            clazz<Context>, \
            typename class_interfaces<import_symbols::clazz>::type, \
            typename class_method_names<import_symbols::clazz>::type> { \
    public: \
        using context_t = Context; \
        using receiver_t = clazz<Context>; \
        using bridge_class_t = import_symbols::clazz; \
        using bridge_symbol_t = import_symbols::clazz; \
        using bridge_direction = import_t; \
        using object_t = typename Context::backend_t::template object_t<receiver_t, import_t>; \
        explicit clazz(Context& ctx, object_t object) \
            noexcept(is_nothrow_move_constructible_v<object_t>) \
            : ctx_(&ctx), object_(std::move(object)) { \
        } \
        clazz(const clazz&) = delete; \
        clazz& operator=(const clazz&) = delete; \
        clazz(clazz&&) noexcept = default; \
        clazz& operator=(clazz&&) noexcept = default; \
        Context& context() noexcept { return *ctx_; } \
        const Context& context() const noexcept { return *ctx_; } \
        receiver_t& receiver() noexcept { return *this; } \
        object_t& object() noexcept { return object_; } \
        const object_t& object() const noexcept { return object_; }
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name) \
        template <typename... Args> \
        decltype(auto) name(Args&&... args) { \
            using member_symbol_t = typename bridge_symbol_t::name; \
            using overloads_t = typename member_symbol_t::overloads_t;
#define DECL_MEMBER_FUNCTION(...)
#define END_MEMBER_CALLABLE_GROUP \
            return import_overload_dispatch<overloads_t>::invoke_member( \
                context(), receiver(), std::forward<Args>(args)...); \
        }
#define END_CLASS \
    private: \
        Context* ctx_; \
        object_t object_; \
    };
    #include DYNABRIDGE_IMPORT_DEF
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
#undef IMPLEMENTS

#undef CALLABLE
#undef OBJECT
}
