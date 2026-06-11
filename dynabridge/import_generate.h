#include "config.h"
#include "import_core.h"

namespace dynabridge {
    namespace import_symbols {
#define BEGIN_CALLABLE_GROUP(name) \
    struct name { \
        static const char* symbol_name() noexcept { return #name; } \
    };
#define DECL_CALLABLE(result, ...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(clazz) \
    struct clazz { \
        using self_symbol_t = clazz; \
        static const char* symbol_name() noexcept { return #clazz; }
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name) \
        struct name { \
            using receiver_symbol_t = self_symbol_t; \
            static const char* symbol_name() noexcept { return #name; } \
        };
#define DECL_MEMBER_FUNCTION(result, ...)
#define END_MEMBER_CALLABLE_GROUP
#define END_CLASS };
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

#define BEGIN_CALLABLE_GROUP(name) \
    template <typename Context, typename... Args> \
    decltype(auto) call_##name(Context& ctx, Args&&... args) { \
        using group_t = import_callable_group<
#define DECL_CALLABLE(result, ...) free_callable<result(__VA_ARGS__)>,
#define DECL_FUNCTION(sig) free_callable<sig>,
#define END_CALLABLE_GROUP \
            free_callable<unmatched_callable_t(unmatched_callable_t)> \
        >; \
        return group_t::invoke(ctx, std::forward<Args>(args)...); \
    }
#define BEGIN_CLASS(clazz)
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(result, ...)
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
        return [&ctx](auto&&... args) -> decltype(auto) { \
            return call_##name(ctx, std::forward<decltype(args)>(args)...); \
        }; \
    }
#define DECL_CALLABLE(result, ...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(clazz)
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(result, ...)
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
#define DECL_CALLABLE(result, ...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(clazz) template <typename Context> class clazz;
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(result, ...)
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
#define DECL_CALLABLE(result, ...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(clazz) \
    template <typename Context, typename... Args> \
    auto construct_##clazz(Context& ctx, Args&&... args) { \
        using receiver_t = clazz<Context>; \
        using group_t = import_constructor_group<
#define DECL_CONSTRUCTOR(...) free_callable<receiver_t(__VA_ARGS__)>,
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define DECL_MEMBER_FUNCTION(result, ...)
#define END_MEMBER_CALLABLE_GROUP
#define END_CLASS \
            free_callable<unmatched_callable_t(unmatched_callable_t)> \
        >; \
        return group_t::construct(ctx, std::forward<Args>(args)...); \
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
#define DECL_CALLABLE(result, ...)
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
#define DECL_MEMBER_FUNCTION(result, ...)
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
#define DECL_CALLABLE(result, ...)
#define DECL_FUNCTION(sig)
#define END_CALLABLE_GROUP
#define BEGIN_CLASS(clazz) \
    template <typename Context> \
    class clazz { \
    public: \
        using context_t = Context; \
        using receiver_t = clazz<Context>; \
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
        object_t& object() noexcept { return object_; } \
        const object_t& object() const noexcept { return object_; }
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name) \
        template <typename... Args> \
        decltype(auto) name(Args&&... args) { \
            using group_t = import_callable_group<
#define DECL_MEMBER_FUNCTION(result, ...) callable<receiver_t, result(__VA_ARGS__)>,
#define END_MEMBER_CALLABLE_GROUP \
                free_callable<unmatched_callable_t(unmatched_callable_t)> \
            >; \
            return group_t::invoke(*ctx_, *this, std::forward<Args>(args)...); \
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
}
