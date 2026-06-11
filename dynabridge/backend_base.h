#ifndef DYNABRIDGE_BACKEND_BASE_H
#define DYNABRIDGE_BACKEND_BASE_H

#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>
#include "traits.h"

namespace dynabridge {
    enum class backend_lifecycle_op {
        destroy
    };

    template <typename>
    struct erased_backend_holder;

    template <typename T>
    const void* type_key() noexcept {
        static const int key = 0;
        return &key;
    }

    template <typename Target>
    class export_class_registry {
    public:
        template <typename Class>
        void store(Target target) {
            const void* const key = type_key<Class>();
            for (auto& entry : entries_) {
                if (entry.key == key) {
                    entry.target = std::move(target);
                    return;
                }
            }
            entries_.emplace_back(key, std::move(target));
        }

        template <typename Class>
        Target& get() {
            const void* const key = type_key<Class>();
            for (auto& entry : entries_) {
                if (entry.key == key) {
                    return entry.target;
                }
            }
            throw std::runtime_error("dynabridge export class is not registered in this context");
        }

    private:
        struct entry_t {
            entry_t(const void* key_, Target target_)
                : key(key_), target(std::move(target_)) {
            }

            const void* key = nullptr;
            Target target;
        };

        std::vector<entry_t> entries_;
    };

    template <typename R, typename... Args>
    struct erased_backend_holder<R(Args...)> {
        using invoke_fn_t = R (*)(erased_backend_holder*, Args...);
        using manager_fn_t = void (*)(erased_backend_holder*, backend_lifecycle_op);

        erased_backend_holder(invoke_fn_t invoke, manager_fn_t manager) noexcept
            : invoke_(invoke), manager_(manager) {
        }

        template <typename R_ = R, std::enable_if_t<!is_void_v<R_>>* = nullptr>
        R_ invoke(Args... args) {
            return invoke_(this, std::forward<Args>(args)...);
        }

        template <typename R_ = R, std::enable_if_t<is_void_v<R_>>* = nullptr>
        void invoke(Args... args) {
            invoke_(this, std::forward<Args>(args)...);
        }

        void manage(backend_lifecycle_op op) noexcept {
            manager_(this, op);
        }

        void destroy() noexcept {
            manage(backend_lifecycle_op::destroy);
        }

        invoke_fn_t invoke_;
        manager_fn_t manager_;
    };

    template <typename Backend, typename = void>
    struct has_backend_dynamic_value : std::false_type {
    };

    template <typename Backend>
    struct has_backend_dynamic_value<Backend, void_t<typename Backend::dynamic_value_t>>
        : std::true_type {
    };

    template <typename Backend, typename = void>
    struct backend_dynamic_value {
    };

    template <typename Backend>
    struct backend_dynamic_value<Backend, void_t<typename Backend::dynamic_value_t>> {
        using type = typename Backend::dynamic_value_t;
    };

    template <typename Backend>
    using backend_dynamic_value_t = typename backend_dynamic_value<Backend>::type;

    template <typename Class, typename = void>
    struct has_export_class_native : std::false_type {
    };

    template <typename Class>
    struct has_export_class_native<Class, void_t<typename Class::native_t>>
        : std::true_type {
    };

    template <typename Class, typename Context, typename ArgList, typename = void>
    struct is_export_class_constructible : std::false_type {
    };

    template <typename Class, typename Context, typename... Args>
    struct is_export_class_constructible<
        Class,
        Context,
        type_list<Args...>,
        void_t<typename Class::native_t>>
        : std::is_constructible<Class, Args...> {
    };

    template <typename Backend, typename Class, typename Context, typename Self, typename = void>
    struct is_export_object_bindable : std::false_type {
    };

    template <typename Backend, typename Class, typename Context, typename Self>
    struct is_export_object_bindable<
        Backend,
        Class,
        Context,
        Self,
        void_t<decltype(Backend::template bind_export_object_impl<Class>(
            std::declval<Context&>(),
            std::declval<Self>()))>>
        : std::true_type {
    };

    template <typename Backend, typename Class, typename Context, typename Target, typename = void>
    struct is_export_class_storable : std::false_type {
    };

    template <typename Backend, typename Class, typename Context, typename Target>
    struct is_export_class_storable<
        Backend,
        Class,
        Context,
        Target,
        void_t<decltype(Backend::template store_export_class_impl<Class>(
            std::declval<Context&>(),
            std::declval<Target>()))>>
        : std::true_type {
    };

    template <typename Backend, typename Class, typename Context, typename ArgList, typename = void>
    struct is_export_object_makable : std::false_type {
    };

    template <typename Backend, typename Class, typename Context, typename... Args>
    struct is_export_object_makable<
        Backend,
        Class,
        Context,
        type_list<Args...>,
        void_t<decltype(Backend::template make_export_object_impl<Class>(
            std::declval<Context&>(),
            std::declval<Args>()...))>>
        : std::true_type {
    };

    template <typename Backend, typename Class, typename Context, typename Target, typename Object, typename = void>
    struct is_export_instance_definable : std::false_type {
    };

    template <typename Backend, typename Class, typename Context, typename Target, typename Object>
    struct is_export_instance_definable<
        Backend,
        Class,
        Context,
        Target,
        Object,
        void_t<decltype(Backend::template define_export_instance_impl<Class>(
            std::declval<Context&>(),
            std::declval<Target&>(),
            std::declval<const char*>(),
            std::declval<Object>()))>>
        : std::true_type {
    };

    template <typename Derived, typename Backend, typename Receiver>
    struct import_object_base {
        template <typename Context, typename... Args>
        void construct_import_object(Context& ctx, Args&&... args) {
            static_assert(
                is_import_object_constructible<Context, Args&&...>::value,
                "Your backend object cannot construct this imported receiver. Implement "
                "object_t<Receiver, import_t>::construct_import_object_impl(ctx, args...).");

            static_cast<Derived&>(*this).construct_import_object_impl(
                ctx, std::forward<Args>(args)...);
        }

    private:
        template <typename Context, typename... Args>
        struct is_import_object_constructible {
            template <typename T, typename = void>
            struct probe : std::false_type {
            };

            template <typename T>
            struct probe<T, void_t<decltype(std::declval<T&>().construct_import_object_impl(
                std::declval<Context&>(),
                std::declval<Args>()...))>>
                : std::true_type {
            };

            constexpr static bool value = probe<Derived>::value;
        };
    };

    template <typename Derived, typename Backend, typename Class>
    struct export_object_base {
        template <typename Context, typename Self, typename... Args>
        void construct_export_object(Context& ctx, Self&& self, Args&&... args) {
            static_assert(
                has_export_class_native<Class>::value,
                "Your exported class has no native type. Add "
                "native_t to the generated export proxy.");
            static_assert(
                is_export_object_constructible<Context, Self&&, Args&&...>::value,
                "Your backend object cannot construct this exported receiver. Implement "
                "object_t<Class, export_t>::construct_export_object_impl(ctx, self, args...).");

            static_cast<Derived&>(*this).construct_export_object_impl(
                ctx, std::forward<Self>(self), std::forward<Args>(args)...);
        }

        template <typename Context, typename... Args>
        static Class* construct_native(Context&, Args&&... args) {
            static_assert(
                is_export_class_constructible<Class, Context, type_list<Args...>>::value,
                "Your exported constructor does not match the generated class proxy.");
            return new Class(std::forward<Args>(args)...);
        }

        template <typename Context, typename Receiver>
        static void destroy_native(Context&, Receiver* receiver) noexcept {
            delete receiver;
        }

    private:
        template <typename Context, typename Self, typename... Args>
        struct is_export_object_constructible {
            template <typename T, typename = void>
            struct probe : std::false_type {
            };

            template <typename T>
            struct probe<T, void_t<decltype(std::declval<T&>().construct_export_object_impl(
                std::declval<Context&>(),
                std::declval<Self>(),
                std::declval<Args>()...))>>
                : std::true_type {
            };

            constexpr static bool value = probe<Derived>::value;
        };
    };

    template <typename Derived, typename Backend, typename Receiver, typename Direction>
    struct object_base_selector;

    template <typename Derived, typename Backend, typename Receiver>
    struct object_base_selector<Derived, Backend, Receiver, import_t> {
        using type = import_object_base<Derived, Backend, Receiver>;
    };

    template <typename Derived, typename Backend, typename Receiver>
    struct object_base_selector<Derived, Backend, Receiver, export_t> {
        using type = export_object_base<Derived, Backend, Receiver>;
    };

    template <typename backend_t>
    struct backend_base {
        template <typename Backend, typename Context, typename Receiver, typename... Args>
        struct is_invocable {
            template <typename T, typename = void>
            struct probe : std::false_type { };

            template <typename T>
            struct probe<T, void_t<
                    decltype(to_cast<Receiver>(
                        std::declval<Context&>(), std::declval<Receiver&>())),
                    decltype(to_cast<Args>(
                        std::declval<Context&>(), std::declval<Args>()))...>>
                : std::true_type { };

            constexpr static bool value = probe<Backend>::value;
        };

        template <typename Backend, typename Context, typename... Args>
        struct is_invocable<Backend, Context, no_receiver_t, Args...> {
            template <typename T, typename = void>
            struct probe : std::false_type { };

            template <typename T>
            struct probe<T, void_t<decltype(to_cast<Args>(
                                std::declval<Context&>(), std::declval<Args>()))...>>
                : std::true_type { };

            constexpr static bool value = probe<Backend>::value;
        };

        template <typename Backend, typename Context, typename Target, typename Binder>
        struct is_definable {
            template <typename T, typename = void>
            struct probe : std::false_type { };

            template <typename T>
            struct probe<T, void_t<
                    decltype(T::define_impl(
                        std::declval<Context&>(),
                        std::declval<Target&>(),
                        std::declval<const char*>(),
                        std::declval<Binder>()))>>
                : std::true_type { };

            constexpr static bool value = probe<Backend>::value;
        };

        template <typename Backend, typename Receiver, typename Context, typename Target>
        struct is_class_definable {
            template <typename T, typename = void>
            struct probe : std::false_type { };

            template <typename T>
            struct probe<T, void_t<
                    decltype(T::template define_class_impl<Receiver>(
                        std::declval<Context&>(),
                        std::declval<Target&>(),
                        std::declval<const char*>()))>>
                : std::true_type { };

            constexpr static bool value = probe<Backend>::value;
        };

        template <typename Backend, typename Receiver, typename Signature, typename Context, typename Target>
        struct is_class_constructor_definable {
            template <typename T, typename = void>
            struct probe : std::false_type { };

            template <typename T>
            struct probe<T, void_t<
                    decltype(T::template define_constructor_impl<Receiver, Signature>(
                        std::declval<Context&>(),
                        std::declval<Target&>()))>>
                : std::true_type { };

            constexpr static bool value = probe<Backend>::value;
        };

        template <typename Backend, typename Class, typename Context, typename Target>
        using is_export_class_storable_t =
            is_export_class_storable<Backend, Class, Context, Target>;

        template <typename Backend, typename Class, typename Context, typename ArgList>
        using is_export_object_makable_t =
            is_export_object_makable<Backend, Class, Context, ArgList>;

        template <typename Backend, typename Class, typename Context, typename Target, typename Object>
        using is_export_instance_definable_t =
            is_export_instance_definable<Backend, Class, Context, Target, Object>;

        template <typename Backend, typename Symbol, typename Context, typename Source>
        struct is_importable {
            template <typename T, typename = void>
            struct probe : std::false_type { };

            template <typename T>
            struct probe<T, void_t<
                    decltype(T::template import_impl<Symbol, Context>(
                        std::declval<Source&>(),
                        std::declval<const char*>()))>>
                : std::true_type { };

            constexpr static bool value = probe<Backend>::value;
        };

        // maybe not useful at all
        backend_t& backend() noexcept {
            return static_cast<backend_t&>(*this);
        }

        const backend_t& backend() const noexcept {
            return static_cast<const backend_t&>(*this);
        }

        template <typename Context, typename Target, typename Binder>
        static auto define(Context& ctx, Target& target, const char* name, Binder binder) {
            static_assert(
                is_definable<backend_t, Context, Target, Binder>::value,
                "Your backend cannot define this export target. Implement "
                "backend_t::define_impl(ctx, target, name, binder).");

            return backend_t::define_impl(ctx, target, name, std::move(binder));
        }

        template <typename Receiver, typename Context, typename Target>
        static auto define_class(Context& ctx, Target& target, const char* name) {
            static_assert(
                is_class_definable<backend_t, Receiver, Context, Target>::value,
                "Your backend cannot define this class export target. Implement "
                "backend_t::define_class_impl<Receiver>(ctx, target, name).");

            return backend_t::template define_class_impl<Receiver>(ctx, target, name);
        }

        template <typename Class, typename Context, typename Target>
        static void store_export_class(Context& ctx, Target&& target) {
            static_assert(
                is_export_class_storable_t<backend_t, Class, Context, Target&&>::value,
                "Your backend cannot store this exported class target. Implement "
                "backend_t::store_export_class_impl<Class>(ctx, target).");

            backend_t::template store_export_class_impl<Class>(
                ctx,
                std::forward<Target>(target));
        }

        template <typename Class, typename Context, typename Self>
        static auto bind_export_object(Context& ctx, Self&& self) {
            static_assert(
                is_export_object_bindable<backend_t, Class, Context, Self&&>::value,
                "Your backend cannot bind this export object handle. Implement "
                "backend_t::bind_export_object_impl<Class>(ctx, self).");

            return backend_t::template bind_export_object_impl<Class>(
                ctx,
                std::forward<Self>(self));
        }

        template <typename Receiver, typename Signature, typename Context, typename Target>
        static auto define_constructor(Context& ctx, Target& target) {
            static_assert(
                is_class_constructor_definable<backend_t, Receiver, Signature, Context, Target>::value,
                "Your backend cannot define this class constructor. Implement "
                "backend_t::define_constructor_impl<Receiver, Signature>(ctx, target).");

            return backend_t::template define_constructor_impl<Receiver, Signature>(ctx, target);
        }

        template <typename Class, typename Context, typename... Args>
        static auto make_export_object(Context& ctx, Args&&... args) {
            static_assert(
                is_export_object_makable_t<
                    backend_t, Class, Context, type_list<Args&&...>>::value,
                "Your backend cannot make this exported object. Implement "
                "backend_t::make_export_object_impl<Class>(ctx, args...).");

            return backend_t::template make_export_object_impl<Class>(
                ctx, std::forward<Args>(args)...);
        }

        template <typename Class, typename Context, typename Target, typename Object>
        static auto define_export_instance(
            Context& ctx,
            Target& target,
            const char* name,
            Object&& object) {
            static_assert(
                is_export_instance_definable_t<
                    backend_t, Class, Context, Target, Object&&>::value,
                "Your backend cannot define this exported instance. Implement "
                "backend_t::define_export_instance_impl<Class>(ctx, target, name, object).");

            return backend_t::template define_export_instance_impl<Class>(
                ctx, target, name, std::forward<Object>(object));
        }

        template <typename Symbol, typename Context, typename Source>
        static Context import_from(Source& source, const char* name) {
            static_assert(
                is_importable<backend_t, Symbol, Context, Source>::value,
                "Your backend cannot import this symbol from this source. Implement "
                "backend_t::import_impl<Symbol, Context>(source, name).");

            return backend_t::template import_impl<Symbol, Context>(source, name);
        }

        // the magic
        template <typename R, typename Context, typename Receiver, typename... Args>
        static std::enable_if_t<!is_void_v<R>, R> invoke(
            Context& ctx, Receiver& receiver, Args... args) {
            static_assert(is_invocable<backend_t, Context, Receiver, Args...>::value,
                 "Your context is not invocable with Receiver and Args. This might be because "
                 "converter<Receiver>::to is missing for the receiver, or converter<T>::to "
                 "is missing for an argument.");

            return backend_t::template invoke_impl<R, Receiver>(
                ctx,
                to_cast<Receiver>(ctx, receiver),
                to_cast<Args>(ctx, std::move(args))...);
        }

        template <typename R, typename Context, typename Receiver, typename... Args>
        static std::enable_if_t<is_void_v<R>, void> invoke(
            Context& ctx, Receiver& receiver, Args... args) {
            static_assert(is_invocable<backend_t, Context, Receiver, Args...>::value,
                "Your context is not invocable with Receiver and Args. This might be because "
                "converter<Receiver>::to is missing for the receiver, or converter<T>::to "
                "is missing for an argument.");

            backend_t::template invoke_impl<R, Receiver>(
                ctx,
                to_cast<Receiver>(ctx, receiver),
                to_cast<Args>(ctx, std::move(args))...);
        }

        template <typename R, typename Context, typename... Args>
        static std::enable_if_t<!is_void_v<R>, R> invoke(
            Context& ctx, no_receiver_t, Args... args) {
            static_assert(is_invocable<backend_t, Context, no_receiver_t, Args...>::value,
                 "Your context is not invocable with Args. This might be because "
                 "converter<T>::to is missing for an argument.");

            return backend_t::template invoke_impl<R>(
                ctx,
                no_receiver_t{},
                to_cast<Args>(ctx, std::move(args))...);
        }

        template <typename R, typename Context, typename... Args>
        static std::enable_if_t<is_void_v<R>, void> invoke(
            Context& ctx, no_receiver_t, Args... args) {
            static_assert(is_invocable<backend_t, Context, no_receiver_t, Args...>::value,
                "Your context is not invocable with Args. This might be because "
                "converter<T>::to is missing for an argument.");

            backend_t::template invoke_impl<R>(
                ctx,
                no_receiver_t{},
                to_cast<Args>(ctx, std::move(args))...);
        }
    };
}

#endif
