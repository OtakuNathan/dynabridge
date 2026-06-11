#ifndef DYNABRIDGE_BACKENDS_NAPI_H
#define DYNABRIDGE_BACKENDS_NAPI_H

#include <node_api.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "../backend_base.h"
#include "../callable.h"

namespace dynabridge {
    struct napi_backend : backend_base<napi_backend> {
        using dynamic_value_t = napi_value;

        template <typename T>
        struct converter;

        static void check(napi_status status, const char* message) {
            if (status != napi_ok) {
                throw std::runtime_error(message);
            }
        }

        template <typename Receiver, typename Direction = typename bridge_direction<Receiver>::type>
        class object_t
            : public object_base_selector<
                object_t<Receiver, Direction>, napi_backend, Receiver, Direction>::type {
            using export_base_t =
                export_object_base<object_t<Receiver, Direction>, napi_backend, Receiver>;

        public:
            object_t() noexcept = default;

            object_t(napi_env env, napi_value value) : env_(env) {
                if (value != nullptr) {
                    check(napi_create_reference(env_, value, 1, &ref_), "napi_create_reference failed");
                }
            }

            template <
                typename Context,
                typename... Args,
                typename D = Direction,
                std::enable_if_t<std::is_same<D, import_t>::value>* = nullptr>
            object_t(Context& ctx, construct_object_t, Args&&... args)
                : env_(ctx.env()) {
                this->construct_import_object(ctx, std::forward<Args>(args)...);
            }

            template <
                typename Context,
                typename... Args,
                typename D = Direction,
                std::enable_if_t<std::is_same<D, export_t>::value>* = nullptr>
            object_t(Context& ctx, napi_value self, Args&&... args)
                : env_(ctx.env()) {
                this->construct_export_object(ctx, self, std::forward<Args>(args)...);
            }

            object_t(const object_t&) = delete;
            object_t& operator=(const object_t&) = delete;

            object_t(object_t&& other) noexcept : env_(other.env_), ref_(other.release()) {}

            object_t& operator=(object_t&& other) noexcept {
                if (this != &other) {
                    reset();
                    env_ = other.env_;
                    ref_ = other.release();
                }
                return *this;
            }

            ~object_t() noexcept { reset(); }

            napi_value get() const {
                if (ref_ == nullptr) {
                    return nullptr;
                }

                napi_value value = nullptr;
                napi_get_reference_value(env_, ref_, &value);
                return value;
            }

            napi_env env() const noexcept { return env_; }

            template <typename Context, typename R = Receiver>
            R* native(Context&) const {
                void* receiver = nullptr;
                if (ref_ == nullptr || napi_unwrap(env_, get(), &receiver) != napi_ok) {
                    return nullptr;
                }
                return static_cast<R*>(receiver);
            }

            napi_ref release() noexcept {
                napi_ref const ref = ref_;
                ref_ = nullptr;
                return ref;
            }

            void reset() noexcept {
                if (ref_ != nullptr) {
                    napi_delete_reference(env_, ref_);
                    ref_ = nullptr;
                }
            }

            void reset(napi_env env, napi_value value) {
                reset();
                env_ = env;
                if (value != nullptr) {
                    check(napi_create_reference(env_, value, 1, &ref_), "napi_create_reference failed");
                }
            }

            template <typename Context, typename... Args>
            void construct_import_object_impl(Context& ctx, Args&&... args) {
                napi_value argv[sizeof...(Args) == 0 ? 1 : sizeof...(Args)] = {
                    to_cast<typename std::decay<Args>::type>(ctx, std::forward<Args>(args))...
                };
                napi_value value = nullptr;
                check(napi_new_instance(
                    env_, ctx.callable(), sizeof...(Args),
                    sizeof...(Args) == 0 ? nullptr : argv, &value), "napi_new_instance failed");
                reset(env_, value);
            }

            template <typename Context, typename... Args>
            void construct_export_object_impl(Context& ctx, napi_value self, Args&&... args) {
                using receiver_t = Receiver;
                receiver_t* receiver = export_base_t::construct_native(ctx, std::forward<Args>(args)...);
                construct_export_object_from_native(ctx, self, receiver);
            }

            template <typename Context>
            void construct_export_object_from_native(Context& ctx, napi_value self, Receiver* receiver) {
                if (napi_wrap(ctx.env(), self, receiver, native_finalizer<Context, Receiver>, &ctx, nullptr) != napi_ok) {
                    export_base_t::destroy_native(ctx, receiver);
                    throw std::runtime_error("napi_wrap failed");
                }
                reset(ctx.env(), self);
            }

        private:
            template <typename Context, typename Native>
            static void native_finalizer(napi_env, void* data, void* hint) {
                export_base_t::destroy_native(*static_cast<Context*>(hint), static_cast<Native*>(data));
            }

            napi_env env_ = nullptr;
            napi_ref ref_ = nullptr;
        };

        struct module_t {
            napi_env env = nullptr;
            napi_value value = nullptr;

            void define(napi_env, const char* name, napi_value export_value) {
                check(napi_set_named_property(env, value, name, export_value),
                    "napi_set_named_property failed");
            }
        };

    private:
        using class_state_holder = erased_backend_holder<void(napi_env, napi_callback_info, napi_value)>;

        template <typename Receiver, typename Context>
        struct class_state;

    public:
        struct class_target_t {
            napi_env env = nullptr;
            napi_value constructor = nullptr;
            class_state_holder* state = nullptr;

            void define(napi_env, const char* name, napi_value export_value) {
                napi_value prototype = nullptr;
                check(napi_get_named_property(env, constructor, "prototype", &prototype),
                    "napi class prototype lookup failed");
                check(napi_set_named_property(env, prototype, name, export_value),
                    "napi class method definition failed");
            }
        };

        class context_t {
        public:
            using backend_t = napi_backend;

            context_t() noexcept = default;

            // References own long-lived handles; caches are the current-scope
            // hot path. Refresh the caches when reusing a context in a new
            // N-API handle scope.
            context_t(napi_env env, napi_value callable = nullptr)
                : env_(env), callable_(env, callable), callable_cache_(callable) {
                refresh_this_arg();
            }

            context_t(const context_t&) = delete;
            context_t& operator=(const context_t&) = delete;

            context_t(context_t&& other) noexcept
                : env_(other.env_),
                  callable_(std::move(other.callable_)),
                  callable_cache_(other.callable_cache_),
                  this_arg_cache_(other.this_arg_cache_) {
                other.env_ = nullptr;
                other.callable_cache_ = nullptr;
                other.this_arg_cache_ = nullptr;
            }

            context_t& operator=(context_t&& other) noexcept {
                if (this != &other) {
                    env_ = other.env_;
                    callable_ = std::move(other.callable_);
                    callable_cache_ = other.callable_cache_;
                    this_arg_cache_ = other.this_arg_cache_;
                    other.env_ = nullptr;
                    other.callable_cache_ = nullptr;
                    other.this_arg_cache_ = nullptr;
                }
                return *this;
            }

            napi_env env() const noexcept { return env_; }

            napi_value callable() {
                if (callable_cache_ == nullptr) {
                    callable_cache_ = callable_.get();
                }
                return callable_cache_;
            }

            napi_value this_arg() {
                if (this_arg_cache_ == nullptr) {
                    refresh_this_arg();
                }
                return this_arg_cache_;
            }

            void refresh_callable() {
                callable_cache_ = callable_.get();
            }

            void refresh_this_arg() {
                if (env_ != nullptr) {
                    check(napi_get_undefined(env_, &this_arg_cache_), "napi_get_undefined failed");
                }
            }

            void refresh() {
                refresh_callable();
                refresh_this_arg();
            }

            void set_callable(napi_value callable) {
                callable_.reset(env_, callable);
                callable_cache_ = callable;
            }

        private:
            struct callable_tag;

            napi_env env_ = nullptr;
            object_t<callable_tag> callable_;
            napi_value callable_cache_ = nullptr;
            napi_value this_arg_cache_ = nullptr;
        };

        using import_context_t = context_t;

        class export_context_t : public context_t {
        public:
            using context_t::context_t;

            export_context_t() noexcept = default;
            export_context_t(export_context_t&&) noexcept = default;
            export_context_t& operator=(export_context_t&&) noexcept = default;

            template <typename Class>
            void store_export_class(class_target_t target) {
                classes_.template store<Class>(std::move(target));
            }

            template <typename Class>
            class_target_t& export_class() {
                return classes_.template get<Class>();
            }

        private:
            export_class_registry<class_target_t> classes_;
        };

        template <typename R, std::enable_if_t<!is_void_v<R>>* = nullptr, typename... Args>
        static R invoke_impl(context_t& ctx, no_receiver_t, Args... args) {
            napi_value result = call(ctx, ctx.callable(), std::move(args)...);
            return from_cast<R>(ctx, result);
        }

        template <typename R, std::enable_if_t<is_void_v<R>>* = nullptr, typename... Args>
        static void invoke_impl(context_t& ctx, no_receiver_t, Args... args) {
            call(ctx, ctx.callable(), std::move(args)...);
        }

        template <
            typename R, typename Receiver, typename DynamicReceiver,
            std::enable_if_t<!is_void_v<R>>* = nullptr, typename... Args>
        static R invoke_impl(context_t& ctx, DynamicReceiver receiver, Args... args) {
            napi_value result = call(ctx, ctx.callable(), std::move(receiver), std::move(args)...);
            return from_cast<R>(ctx, result);
        }

        template <
            typename R, typename Receiver, typename DynamicReceiver,
            std::enable_if_t<is_void_v<R>>* = nullptr, typename... Args>
        static void invoke_impl(context_t& ctx, DynamicReceiver receiver, Args... args) {
            call(ctx, ctx.callable(), std::move(receiver), std::move(args)...);
        }

        template <typename Context, typename Binder>
        static void define_impl(Context& ctx, class_target_t& target, const char* name, Binder binder) {
            target.define(ctx.env(), name, make_member_function(ctx.env(), name, std::move(binder)));
        }

        template <typename Context, typename Target, typename Binder>
        static void define_impl(Context& ctx, Target& target, const char* name, Binder binder) {
            target.define(ctx.env(), name, make_function(ctx.env(), name, std::move(binder)));
        }

        template <typename Receiver, typename Context, typename Target>
        static class_target_t define_class_impl(Context& ctx, Target& target, const char* name) {
            auto* state = new class_state<Receiver, Context>(ctx);
            napi_value constructor = nullptr;
            if (napi_define_class(ctx.env(), name, NAPI_AUTO_LENGTH, class_constructor, state,
                    0, nullptr, &constructor) != napi_ok) {
                state->destroy();
                throw std::runtime_error("napi_define_class failed");
            }

            if (napi_add_finalizer(
                    ctx.env(), constructor, state, class_state_finalizer, nullptr, nullptr) != napi_ok) {
                state->destroy();
                throw std::runtime_error("napi_add_finalizer failed");
            }

            target.define(ctx.env(), name, constructor);
            return class_target_t{ctx.env(), constructor, state};
        }

        template <typename Receiver, typename Signature, typename Context>
        static void define_constructor_impl(Context&, class_target_t& target) {
            static_cast<class_state<Receiver, Context>*>(target.state)->template set_constructor<Signature>();
        }

        template <typename Class, typename Context>
        static void store_export_class_impl(Context& ctx, class_target_t target) {
            ctx.template store_export_class<Class>(std::move(target));
        }

        template <typename Class, typename Context>
        static object_t<Class, export_t> bind_export_object_impl(Context& ctx, napi_value self) {
            return object_t<Class, export_t>(ctx.env(), self);
        }

        template <typename Class, typename Context, typename... Args>
        static object_t<Class, export_t> make_export_object_impl(
            Context& ctx,
            Args&&... args)
        {
            using object_type = object_t<Class, export_t>;
            using export_base_t = export_object_base<object_type, napi_backend, Class>;
            using state_t = class_state<Class, Context>;
            class_target_t& target = ctx.template export_class<Class>();

            typename state_t::external_proxy_handoff handoff;
            handoff.receiver = export_base_t::construct_native(ctx, std::forward<Args>(args)...);

            napi_value external = nullptr;
            if (napi_create_external(ctx.env(), &handoff, nullptr, nullptr, &external) != napi_ok) {
                export_base_t::destroy_native(ctx, handoff.receiver);
                throw std::runtime_error("napi_create_external failed");
            }

            napi_value value = nullptr;
            if (napi_new_instance(ctx.env(), target.constructor, 1, &external, &value) != napi_ok
                    || handoff.receiver != nullptr) {
                if (handoff.receiver != nullptr) {
                    export_base_t::destroy_native(ctx, handoff.receiver);
                }
                throw std::runtime_error("napi_new_instance failed for exported object");
            }

            return object_type(ctx.env(), value);
        }

        template <typename Class, typename Context, typename Target>
        static void define_export_instance_impl(
            Context& ctx,
            Target& target,
            const char* name,
            object_t<Class, export_t> object)
        {
            target.define(ctx.env(), name, object.get());
        }

        static napi_value undefined(context_t& ctx) {
            napi_value result = nullptr;
            napi_get_undefined(ctx.env(), &result);
            return result;
        }

        template <typename Symbol, typename Context>
        static Context import_impl(module_t& source, const char* name) {
            return Context(source.env, import_property(source.env, source.value, name));
        }

        template <typename Symbol, typename Context, typename SourceTag, typename Direction>
        static Context import_impl(object_t<SourceTag, Direction>& source, const char* name) {
            return Context(source.env(), import_property(source.env(), source.get(), name));
        }

    private:
        static napi_value import_property(napi_env env, napi_value source, const char* name) {
            napi_value result = nullptr;
            check(napi_get_named_property(env, source, name, &result), "N-API import lookup failed");
            return result;
        }

        using callback_holder = erased_backend_holder<napi_value(napi_env, napi_value, napi_value*)>;

        template <typename Receiver, typename Context>
        struct class_state : class_state_holder {
            using construct_fn_t = void (*)(class_state*, napi_env, napi_callback_info, napi_value);

            struct external_proxy_handoff {
                static std::uintptr_t magic_value() noexcept {
                    return static_cast<std::uintptr_t>(0x44594e414558504full);
                }

                std::uintptr_t magic = magic_value();
                Receiver* receiver = nullptr;
            };

            explicit class_state(Context& ctx) noexcept
                : class_state_holder(construct_entry, manage_entry),
                  construct_(missing_constructor_entry),
                  ctx_(&ctx) {
            }

            template <typename Signature>
            void set_constructor() noexcept {
                construct_ = typed_construct_entry<Signature>;
            }

            static void construct_entry(class_state_holder* state, napi_env env, napi_callback_info info, napi_value self) {
                auto* typed_state = static_cast<class_state*>(state);
                if (typed_state->try_construct_external_proxy(env, info, self)) {
                    return;
                }
                typed_state->construct_(typed_state, env, info, self);
            }

            static void manage_entry(class_state_holder* state, backend_lifecycle_op op) noexcept {
                if (op == backend_lifecycle_op::destroy) {
                    delete static_cast<class_state*>(state);
                }
            }

            static void missing_constructor_entry(class_state*, napi_env, napi_callback_info, napi_value) {
                throw std::runtime_error("dynabridge N-API class has no exported constructor");
            }

            template <typename Signature>
            static void typed_construct_entry(class_state* state, napi_env env, napi_callback_info info, napi_value self) {
                state->construct_signature(type_identity<Signature>{}, env, info, self);
            }

            template <typename... Args>
            void construct_signature(type_identity<void(Args...)>, napi_env env, napi_callback_info info, napi_value self) {
                napi_value argv[sizeof...(Args) == 0 ? 1 : sizeof...(Args)] = {};
                std::size_t argc = sizeof...(Args);
                if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok
                        || argc != sizeof...(Args)) {
                    throw std::runtime_error("dynabridge N-API constructor received invalid call info");
                }

                construct_indices<Args...>(env, self, argv, std::index_sequence_for<Args...>{});
            }

            template <typename... Args, std::size_t... Indices>
            void construct_indices(napi_env, napi_value self, napi_value* argv, std::index_sequence<Indices...>) {
                object_t<Receiver, export_t> object(
                    *ctx_, self, from_cast<Args>(*ctx_, argv[Indices])...);
                (void)object;
            }

            bool try_construct_external_proxy(napi_env env, napi_callback_info info, napi_value self) {
                napi_value argv[1] = {};
                std::size_t argc = 1;
                if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc != 1) {
                    return false;
                }

                void* data = nullptr;
                if (napi_get_value_external(env, argv[0], &data) != napi_ok || data == nullptr) {
                    return false;
                }

                auto* handoff = static_cast<external_proxy_handoff*>(data);
                if (handoff->magic != external_proxy_handoff::magic_value()
                        || handoff->receiver == nullptr) {
                    return false;
                }

                object_t<Receiver, export_t> object;
                object.construct_export_object_from_native(*ctx_, self, handoff->receiver);
                handoff->receiver = nullptr;
                return true;
            }

            construct_fn_t construct_;
            Context* ctx_;
        };

        template <typename Binder>
        struct typed_callback_holder : callback_holder {
            using signature_t = typename Binder::signature_t;

            explicit typed_callback_holder(Binder binder)
                : callback_holder(call_entry, manage_entry), binder_(std::move(binder)) {
            }

            static napi_value call_entry(callback_holder* holder, napi_env env, napi_value, napi_value* argv) {
                return static_cast<typed_callback_holder*>(holder)->call_signature(
                    type_identity<typename Binder::signature_t>{}, env, argv);
            }

            static void manage_entry(callback_holder* holder, backend_lifecycle_op op) noexcept {
                if (op == backend_lifecycle_op::destroy) {
                    delete static_cast<typed_callback_holder*>(holder);
                }
            }

            template <typename R, typename... Args>
            napi_value call_signature(type_identity<R(Args...)>, napi_env env, napi_value* argv) {
                return call_indices<R>(env, argv, std::index_sequence_for<Args...>{});
            }

            template <typename R, std::size_t... Indices, std::enable_if_t<!is_void_v<R>>* = nullptr>
            napi_value call_indices(napi_env, napi_value* argv, std::index_sequence<Indices...>) {
                return binder_(argv[Indices]...);
            }

            template <typename R, std::size_t... Indices, std::enable_if_t<is_void_v<R>>* = nullptr>
            napi_value call_indices(napi_env env, napi_value* argv, std::index_sequence<Indices...>) {
                binder_(argv[Indices]...);

                napi_value undefined = nullptr;
                napi_get_undefined(env, &undefined);
                return undefined;
            }

            Binder binder_;
        };

        template <typename Binder>
        struct typed_member_callback_holder : callback_holder {
            using signature_t = typename Binder::signature_t;

            explicit typed_member_callback_holder(Binder binder)
                : callback_holder(call_entry, manage_entry), binder_(std::move(binder)) {
            }

            static napi_value call_entry(callback_holder* holder, napi_env env, napi_value receiver, napi_value* argv) {
                return static_cast<typed_member_callback_holder*>(holder)->call_signature(
                    type_identity<typename Binder::signature_t>{}, env, receiver, argv);
            }

            static void manage_entry(callback_holder* holder, backend_lifecycle_op op) noexcept {
                if (op == backend_lifecycle_op::destroy) {
                    delete static_cast<typed_member_callback_holder*>(holder);
                }
            }

            template <typename R, typename Receiver, typename... Args>
            napi_value call_signature(type_identity<R(Receiver, Args...)>, napi_env env, napi_value receiver,
                napi_value* argv) {
                return call_indices<R>(env, receiver, argv, std::index_sequence_for<Args...>{});
            }

            template <typename R, std::size_t... Indices, std::enable_if_t<!is_void_v<R>>* = nullptr>
            napi_value call_indices(napi_env, napi_value receiver, napi_value* argv, std::index_sequence<Indices...>) {
                return binder_(receiver, argv[Indices]...);
            }

            template <typename R, std::size_t... Indices, std::enable_if_t<is_void_v<R>>* = nullptr>
            napi_value call_indices(napi_env env, napi_value receiver, napi_value* argv, std::index_sequence<Indices...>) {
                binder_(receiver, argv[Indices]...);

                napi_value undefined = nullptr;
                napi_get_undefined(env, &undefined);
                return undefined;
            }

            Binder binder_;
        };

        template <typename Binder>
        struct overload_callback_holder : callback_holder {
            using overloads_t = typename Binder::overloads_t;
            constexpr static std::size_t max_arity = max_callable_arity<overloads_t>::value;
            constexpr static std::size_t buffer_arity = max_arity + 1;

            explicit overload_callback_holder(Binder binder)
                : callback_holder(call_entry, manage_entry), binder_(std::move(binder)) {
            }

            struct argv_accessor {
                constexpr static std::size_t static_arity = max_arity;

                napi_value* argv = nullptr;

                template <std::size_t I>
                napi_value get() const {
                    return argv[I];
                }
            };

            static napi_value call_entry(callback_holder* holder, napi_env env, napi_value, napi_value* argv) {
                (void)holder;
                (void)env;
                (void)argv;
                return nullptr;
            }

            static void manage_entry(callback_holder* holder, backend_lifecycle_op op) noexcept {
                if (op == backend_lifecycle_op::destroy) {
                    delete static_cast<overload_callback_holder*>(holder);
                }
            }

            static napi_value callback(napi_env env, napi_callback_info info) {
                napi_value argv[buffer_arity] = {};
                std::size_t argc = buffer_arity;
                void* data = nullptr;
                if (napi_get_cb_info(env, info, &argc, argv, nullptr, &data) != napi_ok || data == nullptr) {
                    napi_throw_error(env, nullptr, "dynabridge N-API overload callback received invalid call info");
                    return nullptr;
                }

                auto* holder = static_cast<overload_callback_holder*>(data);
                try {
                    auto result = holder->binder_.dispatch_optional(argc, argv_accessor{argv});
                    if (!result) {
                        napi_throw_error(
                            env,
                            nullptr,
                            "dynabridge N-API overload has no matching signature");
                        return nullptr;
                    }
                    return *result;
                } catch (const bad_conversion& error) {
                    napi_throw_error(env, nullptr, error.what());
                    return nullptr;
                } catch (const std::exception& error) {
                    napi_throw_error(env, nullptr, error.what());
                    return nullptr;
                } catch (...) {
                    napi_throw_error(env, nullptr, "dynabridge N-API overload callback failed");
                    return nullptr;
                }
            }

            Binder binder_;
        };

        template <typename... Args>
        static napi_value call(context_t& ctx, napi_value callable, Args... args) {
            napi_value argv[] = { args... };
            napi_value result = nullptr;
            check(napi_call_function(
                ctx.env(), ctx.this_arg(), callable, sizeof...(Args),
                sizeof...(Args) == 0 ? nullptr : argv, &result), "napi_call_function failed");
            return result;
        }

        template <typename Holder, typename Signature>
        struct callback_dispatch;

        template <typename Holder, typename R, typename... Args>
        struct callback_dispatch<Holder, R(Args...)> {
            static napi_value call(napi_env env, napi_callback_info info) {
                napi_value argv[sizeof...(Args) == 0 ? 1 : sizeof...(Args)] = {};
                std::size_t argc = sizeof...(Args);
                void* data = nullptr;
                if (napi_get_cb_info(env, info, &argc, argv, nullptr, &data) != napi_ok
                        || argc != sizeof...(Args)
                        || data == nullptr) {
                    napi_throw_error(env, nullptr, "dynabridge N-API callback received invalid call info");
                    return nullptr;
                }
                try {
                    return static_cast<Holder*>(data)->call_signature(type_identity<R(Args...)>{}, env, argv);
                } catch (const bad_conversion& error) {
                    napi_throw_error(env, nullptr, error.what());
                    return nullptr;
                } catch (const std::exception& error) {
                    napi_throw_error(env, nullptr, error.what());
                    return nullptr;
                } catch (...) {
                    napi_throw_error(env, nullptr, "dynabridge N-API callback failed");
                    return nullptr;
                }
            }
        };

        template <typename Holder, typename Signature>
        struct member_callback_dispatch;

        template <typename Holder, typename R, typename Receiver, typename... Args>
        struct member_callback_dispatch<Holder, R(Receiver, Args...)> {
            static napi_value call(napi_env env, napi_callback_info info) {
                napi_value argv[sizeof...(Args) == 0 ? 1 : sizeof...(Args)] = {};
                napi_value receiver = nullptr;
                std::size_t argc = sizeof...(Args);
                void* data = nullptr;
                if (napi_get_cb_info(env, info, &argc, argv, &receiver, &data) != napi_ok
                        || argc != sizeof...(Args)
                        || data == nullptr) {
                    napi_throw_error(env, nullptr, "dynabridge N-API callback received invalid call info");
                    return nullptr;
                }
                try {
                    return static_cast<Holder*>(data)->call_signature(
                        type_identity<R(Receiver, Args...)>{}, env, receiver, argv);
                } catch (const bad_conversion& error) {
                    napi_throw_error(env, nullptr, error.what());
                    return nullptr;
                } catch (const std::exception& error) {
                    napi_throw_error(env, nullptr, error.what());
                    return nullptr;
                } catch (...) {
                    napi_throw_error(env, nullptr, "dynabridge N-API callback failed");
                    return nullptr;
                }
            }
        };

        template <typename Binder>
        static napi_value make_function(napi_env env, const char* name, Binder binder) {
            using binder_t = typename std::decay<Binder>::type;
            return make_function_impl(env, name, std::move(binder), is_export_overload_binder<binder_t>{});
        }

        template <typename Binder>
        static napi_value make_function_impl(napi_env env, const char* name, Binder binder, std::false_type) {
            using holder_t = typed_callback_holder<typename std::decay<Binder>::type>;
            auto* holder = new holder_t(std::move(binder));

            napi_value function = nullptr;
            if (napi_create_function(env, name, NAPI_AUTO_LENGTH,
                    callback_dispatch<holder_t, typename holder_t::signature_t>::call,
                    holder, &function) != napi_ok) {
                holder->destroy();
                throw std::runtime_error("napi_create_function failed");
            }

            if (napi_add_finalizer(env, function, holder, callback_holder_finalizer, nullptr, nullptr) != napi_ok) {
                holder->destroy();
                throw std::runtime_error("napi_add_finalizer failed");
            }

            return function;
        }

        template <typename Binder>
        static napi_value make_function_impl(napi_env env, const char* name, Binder binder, std::true_type) {
            using holder_t = overload_callback_holder<typename std::decay<Binder>::type>;
            auto* holder = new holder_t(std::move(binder));

            napi_value function = nullptr;
            if (napi_create_function(env, name, NAPI_AUTO_LENGTH,
                    holder_t::callback, holder, &function) != napi_ok) {
                holder->destroy();
                throw std::runtime_error("napi_create_function failed");
            }

            if (napi_add_finalizer(env, function, holder, callback_holder_finalizer, nullptr, nullptr) != napi_ok) {
                holder->destroy();
                throw std::runtime_error("napi_add_finalizer failed");
            }

            return function;
        }

        template <typename Binder>
        static napi_value make_member_function(napi_env env, const char* name, Binder binder) {
            using holder_t = typed_member_callback_holder<typename std::decay<Binder>::type>;
            auto* holder = new holder_t(std::move(binder));

            napi_value function = nullptr;
            if (napi_create_function(env, name, NAPI_AUTO_LENGTH,
                    member_callback_dispatch<holder_t, typename holder_t::signature_t>::call,
                    holder, &function) != napi_ok) {
                holder->destroy();
                throw std::runtime_error("napi_create_function failed");
            }

            if (napi_add_finalizer(env, function, holder, callback_holder_finalizer, nullptr, nullptr) != napi_ok) {
                holder->destroy();
                throw std::runtime_error("napi_add_finalizer failed");
            }

            return function;
        }

        static void callback_holder_finalizer(napi_env, void* data, void*) {
            static_cast<callback_holder*>(data)->destroy();
        }

        static void class_state_finalizer(napi_env, void* data, void*) {
            static_cast<class_state_holder*>(data)->destroy();
        }

        static napi_value class_constructor(napi_env env, napi_callback_info info) {
            napi_value self = nullptr;
            std::size_t argc = 0;
            void* data = nullptr;
            if (napi_get_cb_info(env, info, &argc, nullptr, &self, &data) != napi_ok) {
                return nullptr;
            }

            if (self == nullptr) {
                napi_create_object(env, &self);
            }

            try {
                if (data != nullptr) {
                    static_cast<class_state_holder*>(data)->invoke(env, info, self);
                }
                return self;
            } catch (const bad_conversion& error) {
                napi_throw_error(env, nullptr, error.what());
                return nullptr;
            } catch (const std::exception& error) {
                napi_throw_error(env, nullptr, error.what());
                return nullptr;
            }
        }
    };
}

#include "napi_converters.h"

#endif //DYNABRIDGE_BACKENDS_NAPI_H
