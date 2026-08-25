#define DYNABRIDGE_IMPORT_DEF "tests/import.def"
#define DYNABRIDGE_EXPORT_DEF "tests/export.def"

namespace dynabridge {
    namespace native {
        class counter {
        public:
            explicit counter(unsigned value) noexcept : value_(value) {}

            int add(int value) const noexcept {
                return static_cast<int>(value_) + value;
            }

            int value() const noexcept {
                return static_cast<int>(value_);
            }

        private:
            unsigned value_;
        };
    }
}

#include "dynabridge/bridge.h"
#include "dynabridge/backends/napi.h"

#include <node_api.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

#if defined(DYNABRIDGE_HAS_NODE_ADDON_API)
#include <napi.h>
#endif

namespace {
    using napi_context_t = dynabridge::napi_backend::context_t;

    int add_function(int a, unsigned b) {
        return a + static_cast<int>(b);
    }

    struct transform_function {
        int operator()(int value) const noexcept {
            return value + 2;
        }

        int operator()(int value, unsigned extra) const noexcept {
            return value + static_cast<int>(extra);
        }
    };

    napi_value throw_error(napi_env env, const char* message) {
        napi_throw_error(env, nullptr, message);
        return nullptr;
    }

    int get_int(napi_env env, napi_value value) {
        int result = 0;
        if (napi_get_value_int32(env, value, &result) != napi_ok) {
            throw std::runtime_error("napi_get_value_int32 failed");
        }
        return result;
    }

    unsigned get_uint(napi_env env, napi_value value) {
        unsigned result = 0;
        if (napi_get_value_uint32(env, value, &result) != napi_ok) {
            throw std::runtime_error("napi_get_value_uint32 failed");
        }
        return result;
    }

    std::uint32_t get_iterations(napi_env env, napi_value value) {
        std::uint32_t result = 0;
        if (napi_get_value_uint32(env, value, &result) != napi_ok) {
            throw std::runtime_error("napi_get_value_uint32 failed");
        }
        return result;
    }

    napi_value make_int(napi_env env, int value) {
        napi_value result = nullptr;
        if (napi_create_int32(env, value, &result) != napi_ok) {
            throw std::runtime_error("napi_create_int32 failed");
        }
        return result;
    }

    napi_value get_undefined(napi_env env) {
        napi_value result = nullptr;
        if (napi_get_undefined(env, &result) != napi_ok) {
            throw std::runtime_error("napi_get_undefined failed");
        }
        return result;
    }

    napi_value raw_add(napi_env env, napi_callback_info info) {
        try {
            std::size_t argc = 2;
            napi_value argv[2] = {};
            if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok
                    || argc != 2) {
                return throw_error(env, "rawAdd expects two arguments");
            }

            return make_int(env, add_function(get_int(env, argv[0]), get_uint(env, argv[1])));
        } catch (const std::exception& error) {
            return throw_error(env, error.what());
        }
    }

    napi_value raw_calc(napi_env env, napi_callback_info info) {
        try {
            std::size_t argc = 2;
            napi_value argv[2] = {};
            if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok) {
                return throw_error(env, "rawCalc received invalid call info");
            }

            if (argc == 1) {
                return make_int(env, get_int(env, argv[0]) * 10);
            }
            if (argc == 2) {
                return make_int(env, add_function(get_int(env, argv[0]), get_uint(env, argv[1])));
            }

            return throw_error(env, "rawCalc expects one or two arguments");
        } catch (const std::exception& error) {
            return throw_error(env, error.what());
        }
    }

    napi_value raw_transform_callback(napi_env env, napi_callback_info info);

    napi_value raw_call_loop(napi_env env, napi_callback_info info) {
        try {
            std::size_t argc = 2;
            napi_value argv[2] = {};
            if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok
                    || argc != 2) {
                return throw_error(env, "rawCallLoop expects function and iteration count");
            }

            const std::uint32_t iterations = get_iterations(env, argv[1]);
            napi_value global = nullptr;
            if (napi_get_global(env, &global) != napi_ok) {
                return throw_error(env, "napi_get_global failed");
            }

            int checksum = 0;
            for (std::uint32_t i = 0; i < iterations; ++i) {
                napi_value args[2] = {
                    make_int(env, 1),
                    make_int(env, 2)
                };
                napi_value result = nullptr;
                if (napi_call_function(env, global, argv[0], 2, args, &result) != napi_ok) {
                    return throw_error(env, "napi_call_function failed");
                }
                checksum += get_int(env, result);
            }

            return make_int(env, checksum);
        } catch (const std::exception& error) {
            return throw_error(env, error.what());
        }
    }

    napi_value dynabridge_call_loop(napi_env env, napi_callback_info info) {
        try {
            std::size_t argc = 2;
            napi_value argv[2] = {};
            if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok
                    || argc != 2) {
                return throw_error(env, "dynabridgeCallLoop expects function and iteration count");
            }

            napi_context_t ctx(env, argv[0]);
            const std::uint32_t iterations = get_iterations(env, argv[1]);
            int checksum = 0;
            for (std::uint32_t i = 0; i < iterations; ++i) {
                checksum += dynabridge::call_calc(ctx, 1, 2u);
            }

            return make_int(env, checksum);
        } catch (const std::exception& error) {
            return throw_error(env, error.what());
        }
    }

    napi_value raw_object_call_loop(napi_env env, napi_callback_info info) {
        try {
            std::size_t argc = 3;
            napi_value argv[3] = {};
            if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok
                    || argc != 3) {
                return throw_error(env,
                    "rawObjectCallLoop expects function, object and iteration count");
            }
            const std::uint32_t iterations = get_iterations(env, argv[2]);
            napi_value receiver = get_undefined(env);
            int checksum = 0;
            for (std::uint32_t i = 0; i < iterations; ++i) {
                napi_value args[2] = { argv[1], make_int(env, 1) };
                napi_value result = nullptr;
                if (napi_call_function(env, receiver, argv[0], 2, args, &result) != napi_ok) {
                    return throw_error(env, "raw object import call failed");
                }
                checksum += get_int(env, result);
            }
            return make_int(env, checksum);
        } catch (const std::exception& error) {
            return throw_error(env, error.what());
        }
    }

    napi_value raw_callback_call_loop(napi_env env, napi_callback_info info) {
        try {
            std::size_t argc = 2;
            napi_value argv[2] = {};
            if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok
                    || argc != 2) {
                return throw_error(env,
                    "rawCallbackCallLoop expects function and iteration count");
            }
            const std::uint32_t iterations = get_iterations(env, argv[1]);
            napi_value receiver = get_undefined(env);
            int checksum = 0;
            for (std::uint32_t i = 0; i < iterations; ++i) {
                napi_value callback = nullptr;
                if (napi_create_function(
                        env, "rawTransform", NAPI_AUTO_LENGTH,
                        raw_transform_callback, nullptr, &callback) != napi_ok) {
                    return throw_error(env, "raw callback allocation failed");
                }
                napi_value value = make_int(env, 1);
                napi_value args[2] = { callback, value };
                napi_value result = nullptr;
                if (napi_call_function(env, receiver, argv[0], 2, args, &result) != napi_ok) {
                    return throw_error(env, "raw callback import call failed");
                }
                checksum += get_int(env, result);
            }
            return make_int(env, checksum);
        } catch (const std::exception& error) {
            return throw_error(env, error.what());
        }
    }

    void raw_counter_finalizer(napi_env, void* data, void*) {
        delete static_cast<dynabridge::native::counter*>(data);
    }

    napi_value raw_counter_constructor(napi_env env, napi_callback_info info) {
        try {
            std::size_t argc = 1;
            napi_value argv[1] = {};
            napi_value self = nullptr;
            if (napi_get_cb_info(env, info, &argc, argv, &self, nullptr) != napi_ok
                    || argc != 1) {
                return throw_error(env, "RawCounter expects one argument");
            }
            auto* counter = new dynabridge::native::counter(get_uint(env, argv[0]));
            if (napi_wrap(env, self, counter, raw_counter_finalizer, nullptr, nullptr) != napi_ok) {
                delete counter;
                return throw_error(env, "napi_wrap failed for RawCounter");
            }
            return self;
        } catch (const std::exception& error) {
            return throw_error(env, error.what());
        }
    }

    dynabridge::native::counter* raw_counter(napi_env env, napi_value value) {
        void* data = nullptr;
        if (napi_unwrap(env, value, &data) != napi_ok || data == nullptr) {
            throw std::runtime_error("napi_unwrap failed for RawCounter");
        }
        return static_cast<dynabridge::native::counter*>(data);
    }

    napi_value raw_counter_add(napi_env env, napi_callback_info info) {
        try {
            std::size_t argc = 1;
            napi_value argv[1] = {};
            napi_value self = nullptr;
            if (napi_get_cb_info(env, info, &argc, argv, &self, nullptr) != napi_ok
                    || argc != 1) {
                return throw_error(env, "RawCounter.add expects one argument");
            }
            return make_int(env, raw_counter(env, self)->add(get_int(env, argv[0])));
        } catch (const std::exception& error) {
            return throw_error(env, error.what());
        }
    }

    napi_value raw_counter_value(napi_env env, napi_callback_info info) {
        try {
            std::size_t argc = 0;
            napi_value self = nullptr;
            if (napi_get_cb_info(env, info, &argc, nullptr, &self, nullptr) != napi_ok) {
                return throw_error(env, "RawCounter.value received invalid call info");
            }
            return make_int(env, raw_counter(env, self)->value());
        } catch (const std::exception& error) {
            return throw_error(env, error.what());
        }
    }

    napi_value raw_consume_counter(napi_env env, napi_callback_info info) {
        try {
            std::size_t argc = 2;
            napi_value argv[2] = {};
            if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok
                    || argc != 2) {
                return throw_error(env, "rawConsumeCounter expects object and int");
            }
            return make_int(env,
                raw_counter(env, argv[0])->add(get_int(env, argv[1])));
        } catch (const std::exception& error) {
            return throw_error(env, error.what());
        }
    }

    napi_value raw_use_callback(napi_env env, napi_callback_info info) {
        try {
            std::size_t argc = 2;
            napi_value argv[2] = {};
            if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok
                    || argc != 2) {
                return throw_error(env, "rawUseCallback expects function and int");
            }
            napi_value receiver = get_undefined(env);
            napi_value result = nullptr;
            if (napi_call_function(env, receiver, argv[0], 1, &argv[1], &result) != napi_ok) {
                return throw_error(env, "raw callback invocation failed");
            }
            return result;
        } catch (const std::exception& error) {
            return throw_error(env, error.what());
        }
    }

    napi_value raw_transform_callback(napi_env env, napi_callback_info info) {
        try {
            std::size_t argc = 1;
            napi_value argv[1] = {};
            if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok
                    || argc != 1) {
                return throw_error(env, "raw transform expects one argument");
            }
            return make_int(env, get_int(env, argv[0]) + 2);
        } catch (const std::exception& error) {
            return throw_error(env, error.what());
        }
    }

    napi_value dynabridge_object_call_loop(napi_env env, napi_callback_info info) {
        try {
            std::size_t argc = 3;
            napi_value argv[3] = {};
            if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok
                    || argc != 3) {
                return throw_error(env,
                    "dynabridgeObjectCallLoop expects function, object and iteration count");
            }

            napi_context_t ctx(env, argv[0]);
            auto counter = dynabridge::bind_receiver<dynabridge::counter>(ctx, env, argv[1]);
            const std::uint32_t iterations = get_iterations(env, argv[2]);
            int checksum = 0;
            for (std::uint32_t i = 0; i < iterations; ++i) {
                checksum += dynabridge::call_pass_counter(ctx, counter, 1);
            }
            return make_int(env, checksum);
        } catch (const std::exception& error) {
            return throw_error(env, error.what());
        }
    }

    napi_value dynabridge_callback_call_loop(napi_env env, napi_callback_info info) {
        try {
            std::size_t argc = 2;
            napi_value argv[2] = {};
            if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok
                    || argc != 2) {
                return throw_error(env,
                    "dynabridgeCallbackCallLoop expects function and iteration count");
            }

            napi_context_t ctx(env, argv[0]);
            const std::uint32_t iterations = get_iterations(env, argv[1]);
            int checksum = 0;
            for (std::uint32_t i = 0; i < iterations; ++i) {
                checksum += dynabridge::call_pass_transform(ctx, transform_function{}, 1);
            }
            return make_int(env, checksum);
        } catch (const std::exception& error) {
            return throw_error(env, error.what());
        }
    }

#if defined(DYNABRIDGE_HAS_NODE_ADDON_API)
    class node_counter : public Napi::ObjectWrap<node_counter> {
    public:
        static Napi::Function define(Napi::Env env) {
            return DefineClass(env, "NodeAddonApiCounter", {
                InstanceMethod("add", &node_counter::add),
                InstanceMethod("value", &node_counter::value)
            });
        }

        explicit node_counter(const Napi::CallbackInfo& info)
            : Napi::ObjectWrap<node_counter>(info),
              counter_(info[0].As<Napi::Number>().Uint32Value()) {
        }

        int add_native(int value) const noexcept {
            return counter_.add(value);
        }

    private:
        Napi::Value add(const Napi::CallbackInfo& info) {
            return Napi::Number::New(
                info.Env(), add_native(info[0].As<Napi::Number>().Int32Value()));
        }

        Napi::Value value(const Napi::CallbackInfo& info) {
            return Napi::Number::New(info.Env(), counter_.value());
        }

        dynabridge::native::counter counter_;
    };

    Napi::Value node_addon_api_add(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        const int a = info[0].As<Napi::Number>().Int32Value();
        const unsigned b = info[1].As<Napi::Number>().Uint32Value();
        return Napi::Number::New(env, add_function(a, b));
    }

    Napi::Value node_addon_api_calc(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (info.Length() == 1) {
            return Napi::Number::New(env, info[0].As<Napi::Number>().Int32Value() * 10);
        }
        if (info.Length() == 2) {
            const int a = info[0].As<Napi::Number>().Int32Value();
            const unsigned b = info[1].As<Napi::Number>().Uint32Value();
            return Napi::Number::New(env, add_function(a, b));
        }
        Napi::TypeError::New(env, "nodeAddonApiCalc expects one or two arguments").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Value node_addon_api_call_loop(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        Napi::Function fn = info[0].As<Napi::Function>();
        const std::uint32_t iterations = info[1].As<Napi::Number>().Uint32Value();

        int checksum = 0;
        for (std::uint32_t i = 0; i < iterations; ++i) {
            napi_value args[2] = {
                Napi::Number::New(env, 1),
                Napi::Number::New(env, 2u)
            };
            checksum += fn.Call(2, args).As<Napi::Number>().Int32Value();
        }

        return Napi::Number::New(env, checksum);
    }

    Napi::Value node_addon_api_consume_counter(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        auto* counter = node_counter::Unwrap(info[0].As<Napi::Object>());
        return Napi::Number::New(
            env, counter->add_native(info[1].As<Napi::Number>().Int32Value()));
    }

    Napi::Value node_addon_api_use_callback(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        Napi::Function callback = info[0].As<Napi::Function>();
        napi_value args[1] = { info[1] };
        return callback.Call(1, args);
    }

    Napi::Value node_addon_api_object_call_loop(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        Napi::Function fn = info[0].As<Napi::Function>();
        Napi::Object object = info[1].As<Napi::Object>();
        const std::uint32_t iterations = info[2].As<Napi::Number>().Uint32Value();
        int checksum = 0;
        for (std::uint32_t i = 0; i < iterations; ++i) {
            napi_value args[2] = { object, Napi::Number::New(env, 1) };
            checksum += fn.Call(2, args).As<Napi::Number>().Int32Value();
        }
        return Napi::Number::New(env, checksum);
    }

    Napi::Value node_addon_api_callback_call_loop(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        Napi::Function fn = info[0].As<Napi::Function>();
        const std::uint32_t iterations = info[1].As<Napi::Number>().Uint32Value();
        int checksum = 0;
        for (std::uint32_t i = 0; i < iterations; ++i) {
            Napi::Function callback = Napi::Function::New(
                env,
                [](const Napi::CallbackInfo& callback_info) {
                    return Napi::Number::New(
                        callback_info.Env(),
                        callback_info[0].As<Napi::Number>().Int32Value() + 2);
                },
                "nodeAddonApiTransform");
            napi_value args[2] = { callback, Napi::Number::New(env, 1) };
            checksum += fn.Call(2, args).As<Napi::Number>().Int32Value();
        }
        return Napi::Number::New(env, checksum);
    }
#endif

    void define_raw_function(
        napi_env env,
        dynabridge::napi_backend::module_t& module,
        const char* name,
        napi_callback callback)
    {
        napi_value function = nullptr;
        if (napi_create_function(env, name, NAPI_AUTO_LENGTH, callback, nullptr, &function) != napi_ok) {
            throw std::runtime_error("napi_create_function failed");
        }
        module.define(env, name, function);
    }

    napi_value init(napi_env env, napi_value exports) {
        try {
            static dynabridge::napi_backend::export_context_t ctx;
            ctx = dynabridge::napi_backend::export_context_t(env);
            static dynabridge::napi_backend::trusted_export_context_t trusted_ctx;
            trusted_ctx = dynabridge::napi_backend::trusted_export_context_t(env);
            dynabridge::napi_backend::module_t module{env, exports};
            define_raw_function(env, module, "rawAdd", raw_add);
            define_raw_function(env, module, "rawCalc", raw_calc);
            define_raw_function(env, module, "rawConsumeCounter", raw_consume_counter);
            define_raw_function(env, module, "rawUseCallback", raw_use_callback);
            define_raw_function(env, module, "rawCallLoop", raw_call_loop);
            define_raw_function(env, module, "rawObjectCallLoop", raw_object_call_loop);
            define_raw_function(env, module, "rawCallbackCallLoop", raw_callback_call_loop);
            define_raw_function(env, module, "dynabridgeCallLoop", dynabridge_call_loop);
            define_raw_function(
                env, module, "dynabridgeObjectCallLoop", dynabridge_object_call_loop);
            define_raw_function(
                env, module, "dynabridgeCallbackCallLoop", dynabridge_callback_call_loop);

            napi_value raw_counter_class = nullptr;
            if (napi_define_class(
                    env,
                    "RawCounter",
                    NAPI_AUTO_LENGTH,
                    raw_counter_constructor,
                    nullptr,
                    0,
                    nullptr,
                    &raw_counter_class) != napi_ok) {
                throw std::runtime_error("napi_define_class failed for RawCounter");
            }
            napi_value raw_counter_prototype = nullptr;
            if (napi_get_named_property(
                    env, raw_counter_class, "prototype", &raw_counter_prototype) != napi_ok) {
                throw std::runtime_error("RawCounter prototype lookup failed");
            }
            napi_value raw_add_method = nullptr;
            napi_value raw_value_method = nullptr;
            if (napi_create_function(
                    env, "add", NAPI_AUTO_LENGTH,
                    raw_counter_add, nullptr, &raw_add_method) != napi_ok
                    || napi_create_function(
                        env, "value", NAPI_AUTO_LENGTH,
                        raw_counter_value, nullptr, &raw_value_method) != napi_ok
                    || napi_set_named_property(
                        env, raw_counter_prototype, "add", raw_add_method) != napi_ok
                    || napi_set_named_property(
                        env, raw_counter_prototype, "value", raw_value_method) != napi_ok) {
                throw std::runtime_error("RawCounter method definition failed");
            }
            module.define(env, "rawCounter", raw_counter_class);
            napi_value raw_counter_arg = make_int(env, 2);
            napi_value raw_benchmark_counter = nullptr;
            if (napi_new_instance(
                    env, raw_counter_class, 1,
                    &raw_counter_arg, &raw_benchmark_counter) != napi_ok) {
                throw std::runtime_error("RawCounter construction failed");
            }
            module.define(env, "rawBenchmarkCounter", raw_benchmark_counter);

            dynabridge::export_free_callable(ctx, module, "dynabridgeAdd", add_function);
            dynabridge::export_calc(ctx, module)
                .bind<int(int)>([](int a) {
                    return a * 10;
                })
                .bind<int(int, unsigned)>([](int a, unsigned b) {
                    return add_function(a, b);
                })
                .commit();
            dynabridge::exports::counter::register_all(ctx, module);
            auto exported_counter = dynabridge::make_exported<dynabridge::exports::counter>(
                ctx, dynabridge::native::counter(2u));
            module.define(env, "benchmarkCounter", exported_counter.get());

            napi_value trusted_module_value = nullptr;
            if (napi_create_object(env, &trusted_module_value) != napi_ok) {
                throw std::runtime_error("trusted benchmark module creation failed");
            }
            dynabridge::napi_backend::module_t trusted_module{env, trusted_module_value};
            dynabridge::exports::counter::register_all(trusted_ctx, trusted_module);
            auto trusted_exported_counter =
                dynabridge::make_exported<dynabridge::exports::counter>(
                    trusted_ctx, dynabridge::native::counter(2u));
            module.define(
                env, "trustedBenchmarkCounter", trusted_exported_counter.get());

            using object_signature = int(
                dynabridge::object_param<
                    dynabridge::export_classes::counter,
                    dynabridge::export_t>,
                int);
            dynabridge::export_consume_counter<object_signature>(
                ctx,
                module,
                [](dynabridge::native::counter& counter, int value) {
                    return counter.add(value);
                });

            using callback_signature = int(
                dynabridge::callable_param<
                    dynabridge::import_symbols::callback,
                    dynabridge::import_t>,
                int);
            dynabridge::export_use_callback<callback_signature>(
                ctx,
                module,
                [](auto& callback_ctx, int value) {
                    return dynabridge::call_callback(callback_ctx, value);
                });

#if defined(DYNABRIDGE_HAS_NODE_ADDON_API)
            Napi::Env napi_env_wrap(env);
            Napi::Object exports_object(napi_env_wrap, exports);
            exports_object["nodeAddonApiAdd"] =
                Napi::Function::New(napi_env_wrap, node_addon_api_add, "nodeAddonApiAdd");
            exports_object["nodeAddonApiCalc"] =
                Napi::Function::New(napi_env_wrap, node_addon_api_calc, "nodeAddonApiCalc");
            exports_object["nodeAddonApiCallLoop"] =
                Napi::Function::New(napi_env_wrap, node_addon_api_call_loop, "nodeAddonApiCallLoop");
            Napi::Function node_counter_class = node_counter::define(napi_env_wrap);
            exports_object["nodeAddonApiCounter"] = node_counter_class;
            exports_object["nodeAddonApiBenchmarkCounter"] = node_counter_class.New({
                Napi::Number::New(napi_env_wrap, 2u)
            });
            exports_object["nodeAddonApiConsumeCounter"] = Napi::Function::New(
                napi_env_wrap,
                node_addon_api_consume_counter,
                "nodeAddonApiConsumeCounter");
            exports_object["nodeAddonApiUseCallback"] = Napi::Function::New(
                napi_env_wrap,
                node_addon_api_use_callback,
                "nodeAddonApiUseCallback");
            exports_object["nodeAddonApiObjectCallLoop"] = Napi::Function::New(
                napi_env_wrap,
                node_addon_api_object_call_loop,
                "nodeAddonApiObjectCallLoop");
            exports_object["nodeAddonApiCallbackCallLoop"] = Napi::Function::New(
                napi_env_wrap,
                node_addon_api_callback_call_loop,
                "nodeAddonApiCallbackCallLoop");
#endif

            napi_value has_node_addon_api = nullptr;
            napi_get_boolean(
                env,
#if defined(DYNABRIDGE_HAS_NODE_ADDON_API)
                true,
#else
                false,
#endif
                &has_node_addon_api);
            module.define(env, "hasNodeAddonApi", has_node_addon_api);

            return exports;
        } catch (const std::exception& error) {
            return throw_error(env, error.what());
        }
    }
}

NAPI_MODULE(dynabridge_node_call_benchmark, init)
