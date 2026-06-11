#ifndef DYNABRIDGE_TEST_FAKE_BACKEND_H
#define DYNABRIDGE_TEST_FAKE_BACKEND_H

#include <map>
#include <type_traits>
#include <utility>

#include "dynabridge/traits.h"
#include "dynabridge/backend_base.h"

namespace dynabridge {

    struct fake_backend : backend_base<fake_backend> {
        using dynamic_value_t = int;

        template <typename T>
        struct converter;

        template <
            typename Receiver,
            typename Direction = typename bridge_direction<Receiver>::type>
        class object_t
            : public object_base_selector<
                object_t<Receiver, Direction>, fake_backend, Receiver, Direction>::type {
        public:
            object_t() noexcept = default;

            object_t(unsigned handle) noexcept
                : handle_(handle) {
            }

            template <
                typename Context,
                typename... Args,
                typename D = Direction,
                std::enable_if_t<std::is_same<D, export_t>::value>* = nullptr>
            object_t(Context&, Args&&... args)
                : handle_(next_handle())
            {
                native_ = new Receiver(std::forward<Args>(args)...);
                destroy_native_ = &destroy_cached_native<Receiver>;
                registry()[handle_] = native_;
                registered_ = true;
            }

            template <
                typename Context,
                typename Arg,
                typename D = Direction,
                std::enable_if_t<std::is_same<D, import_t>::value>* = nullptr>
            object_t(Context& ctx, construct_object_t, Arg&& arg)
            {
                this->construct_import_object(ctx, std::forward<Arg>(arg));
            }

            object_t(const object_t&) = delete;
            object_t& operator=(const object_t&) = delete;

            object_t(object_t&& other) noexcept
                : handle_(other.handle_),
                  native_(other.native_),
                  destroy_native_(other.destroy_native_),
                  registered_(other.registered_) {
                other.handle_ = 0;
                other.native_ = nullptr;
                other.destroy_native_ = nullptr;
                other.registered_ = false;
            }

            object_t& operator=(object_t&& other) noexcept {
                if (this != &other) {
                    reset();
                    handle_ = other.handle_;
                    native_ = other.native_;
                    destroy_native_ = other.destroy_native_;
                    registered_ = other.registered_;
                    other.handle_ = 0;
                    other.native_ = nullptr;
                    other.destroy_native_ = nullptr;
                    other.registered_ = false;
                }
                return *this;
            }

            ~object_t() noexcept {
                reset();
            }

            unsigned get() const noexcept {
                return handle_;
            }

            void reset(unsigned handle = 0) noexcept {
                reset_native();
                handle_ = handle;
            }

            unsigned release() noexcept {
                const unsigned handle = handle_;
                handle_ = 0;
                return handle;
            }

            explicit operator bool() const noexcept {
                return handle_ != 0;
            }

            template <typename Context, typename Arg>
            void construct_import_object_impl(Context& ctx, Arg&& arg) {
                handle_ = ctx.callable_(to_cast<typename std::decay<Arg>::type>(
                    ctx, std::forward<Arg>(arg)));
            }

            template <typename Context, typename R = Receiver>
            R* native(Context&) const {
                auto iter = registry().find(handle_);
                if (iter != registry().end()) {
                    return static_cast<R*>(iter->second);
                }
                if (native_ == nullptr) {
                    native_ = new R(handle_);
                    destroy_native_ = &destroy_cached_native<R>;
                }
                return static_cast<R*>(native_);
            }

        private:
            static unsigned next_handle() noexcept {
                static unsigned value = 1000;
                return ++value;
            }

            static std::map<unsigned, void*>& registry() {
                static std::map<unsigned, void*> values;
                return values;
            }

            template <typename Native>
            static void destroy_cached_native(void* native) noexcept {
                delete static_cast<Native*>(native);
            }

            void reset_native() const noexcept {
                if (native_ != nullptr) {
                    if (registered_) {
                        registry().erase(handle_);
                        registered_ = false;
                    }
                    destroy_native_(native_);
                    native_ = nullptr;
                    destroy_native_ = nullptr;
                }
            }

            unsigned handle_ = 0;
            mutable void* native_ = nullptr;
            mutable void (*destroy_native_)(void*) = nullptr;
            mutable bool registered_ = false;
        };

        template <typename Callable>
        struct context_t {
            using backend_t = fake_backend;
            using callable_t = Callable;

            Callable callable_;
            int to_count = 0;
            int from_count = 0;

            context_t(Callable&& callable)
            noexcept(is_nothrow_move_constructible_v<Callable>)
                : callable_(std::move(callable)) {
            }

            void reset_conversions() noexcept {
                to_count = 0;
                from_count = 0;
            }
        };

        template <typename Callable>
        using import_context_t = context_t<Callable>;

        template <typename Callable>
        struct export_context_t : context_t<Callable> {
            using context_t<Callable>::context_t;

            template <typename Class, typename Target>
            void store_export_class(Target&&) noexcept {
            }
        };

        template <
            typename R,
            typename Callable,
            std::enable_if_t<!is_void_v<R>>* = nullptr,
            typename... Args>
        static R invoke_impl(context_t<Callable>& ctx, no_receiver_t, Args... args) {
            return from_cast<R>(ctx, ctx.callable_(std::move(args)...));
        }

        template <
            typename R,
            typename Callable,
            std::enable_if_t<is_void_v<R>>* = nullptr,
            typename... Args>
        static void invoke_impl(context_t<Callable>& ctx, no_receiver_t, Args... args) {
            ctx.callable_(std::move(args)...);
        }

        template <
            typename R,
            typename Receiver,
            typename DynamicReceiver,
            typename Callable,
            std::enable_if_t<!is_void_v<R>>* = nullptr,
            typename... Args>
        static R invoke_impl(context_t<Callable>& ctx, DynamicReceiver receiver, Args... args) {
            return from_cast<R>(
                ctx,
                ctx.callable_(std::move(receiver), std::move(args)...));
        }

        template <
            typename R,
            typename Receiver,
            typename DynamicReceiver,
            typename Callable,
            std::enable_if_t<is_void_v<R>>* = nullptr,
            typename... Args>
        static void invoke_impl(context_t<Callable>& ctx, DynamicReceiver receiver, Args... args) {
            ctx.callable_(std::move(receiver), std::move(args)...);
        }

        template <typename Context, typename Target, typename Binder>
        static auto define_impl(Context&, Target& target, const char* name, Binder binder)
            -> decltype(target.def(name, std::move(binder)))
        {
            return target.def(name, std::move(binder));
        }

        template <typename Receiver, typename Context, typename Target>
        static auto define_class_impl(Context&, Target& target, const char* name)
            -> decltype(target.template def_class<Receiver>(name))
        {
            return target.template def_class<Receiver>(name);
        }

        template <typename Class, typename Signature, typename Context, typename Target>
        static auto define_constructor_impl(Context&, Target& target)
            -> decltype(target.template def_constructor<Class, Signature>())
        {
            return target.template def_constructor<Class, Signature>();
        }

        template <typename Class, typename Context, typename Target>
        static void store_export_class_impl(Context& ctx, Target&& target) {
            ctx.template store_export_class<Class>(std::forward<Target>(target));
        }

        template <typename Class, typename Context>
        static object_t<Class, export_t> bind_export_object_impl(Context&, unsigned handle) {
            return object_t<Class, export_t>(handle);
        }

        template <typename Class, typename Context, typename... Args>
        static object_t<Class, export_t> make_export_object_impl(
            Context& ctx,
            Args&&... args)
        {
            return object_t<Class, export_t>(ctx, std::forward<Args>(args)...);
        }

        template <typename Class, typename Context, typename Target>
        static void define_export_instance_impl(
            Context&,
            Target& target,
            const char* name,
            object_t<Class, export_t> object)
        {
            target.def_instance(name, std::move(object));
        }
    };

    
    template <>
    struct fake_backend::converter<int> {
        template <typename T>
        static int to(context_t<T>& ctx, int n) noexcept {
            ++ctx.to_count;
            return n;
        }

        template <typename T>
        static optional<int> from(context_t<T>& ctx, int n) noexcept {
            ++ctx.from_count;
            return optional<int>(n);
        }
    };

    template <>
    struct fake_backend::converter<unsigned> {
        template <typename T>
        static unsigned to(context_t<T>& ctx, unsigned n) noexcept  {
            ++ctx.to_count;
            return n;
        }

        template <typename T>
        static optional<unsigned> from(context_t<T>& ctx, int n) noexcept  {
            ++ctx.from_count;
            if (n < 0) {
                return optional<unsigned>();
            }
            return optional<unsigned>(static_cast<unsigned>(n));
        }
    };
    }

#endif //DYNABRIDGE_TEST_FAKE_BACKEND_H
