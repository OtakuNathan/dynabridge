#ifndef DYNABRIDGE_EXPORT_CORE_H
#define DYNABRIDGE_EXPORT_CORE_H

#include <type_traits>
#include <utility>

#include "export_callable.h"

namespace dynabridge {
    template <typename Class>
    struct export_class_registrar;

    template <typename Class>
    struct export_constructor_group_for {
        using type = callable_group<free_callable<unmatched_callable_t(unmatched_callable_t)>>;
    };

    template <typename Member, typename Signature>
    struct export_member_forwarder;

    template <typename Member, typename R, typename... Args>
    struct export_member_forwarder<Member, R(Args...)> {
        R operator()(typename Member::receiver_t& receiver, Args... args) const {
            return Member::call(receiver, std::forward<Args>(args)...);
        }
    };

    template <typename Class, typename Context, typename Target>
    class export_class_registration {
    public:
        using context_t = Context;
        using class_t = Class;
        using backend_t = typename Context::backend_t;
        using class_target_t = Target;

        export_class_registration(Context& ctx, Target& target)
            : ctx_(&ctx),
              target_(&target) {
        }

        Context& context() noexcept { return *ctx_; }
        class_target_t& target() noexcept { return *target_; }
        const class_target_t& target() const noexcept { return *target_; }

        template <typename... Args>
        auto constructor()
            -> decltype(backend_t::template define_constructor<class_t, void(Args...)>(
                std::declval<Context&>(), std::declval<class_target_t&>()))
        {
            using signature_t = void(Args...);
            using group_t = typename export_constructor_group_for<class_t>::type;
            static_assert(
                is_declared_free_callable<signature_t, group_t>::value,
                "This class does not declare this constructor signature.");
            static_assert(
                is_export_class_constructible<class_t, Context, type_list<Args...>>::value,
                "This exported constructor does not match the native class.");
            return backend_t::template define_constructor<class_t, signature_t>(
                *ctx_, *target_);
        }

    private:
        template <typename>
        friend struct export_class_registrar;

        template <typename Signature, typename Callable>
        auto member(const char* name, Callable&& callable)
            -> decltype(export_member_callable<class_t, Signature>(
                std::declval<Context&>(),
                std::declval<class_target_t&>(),
                name,
                std::forward<Callable>(callable)))
        {
            using callable_t = typename std::decay<Callable>::type;
            static_assert(
                is_export_member_callable_bindable<class_t, Signature, Context, callable_t>::value,
                "Your export member is not bindable with this Signature.");
            return export_member_callable<class_t, Signature>(
                *ctx_, *target_, name, std::forward<Callable>(callable));
        }

        Context* ctx_;
        class_target_t* target_;
    };

    template <typename Class, typename Context, typename... Args>
    auto make_exported(Context& ctx, Args&&... args) {
        using backend_t = typename Context::backend_t;
        return backend_t::template make_export_object<Class>(
            ctx, std::forward<Args>(args)...);
    }

    template <typename Class, typename Context, typename Target, typename... Args>
    auto export_instance(Context& ctx, Target& target, const char* name, Args&&... args) {
        using backend_t = typename Context::backend_t;
        auto object = make_exported<Class>(ctx, std::forward<Args>(args)...);
        return backend_t::template define_export_instance<Class>(
            ctx, target, name, std::move(object));
    }
}

#endif //DYNABRIDGE_EXPORT_CORE_H
