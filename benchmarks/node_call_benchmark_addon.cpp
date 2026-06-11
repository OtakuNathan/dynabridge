#define DYNABRIDGE_IMPORT_DEF "tests/import.def"
#define DYNABRIDGE_EXPORT_DEF "tests/export.def"
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

#if defined(DYNABRIDGE_HAS_NODE_ADDON_API)
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
            dynabridge::napi_backend::module_t module{env, exports};

            define_raw_function(env, module, "rawAdd", raw_add);
            define_raw_function(env, module, "rawCalc", raw_calc);
            define_raw_function(env, module, "rawCallLoop", raw_call_loop);
            define_raw_function(env, module, "dynabridgeCallLoop", dynabridge_call_loop);

            dynabridge::export_free_callable(ctx, module, "dynabridgeAdd", add_function);
            dynabridge::export_calc(ctx, module)
                .bind<int(int)>([](int a) {
                    return a * 10;
                })
                .bind<int(int, unsigned)>([](int a, unsigned b) {
                    return add_function(a, b);
                })
                .commit();

#if defined(DYNABRIDGE_HAS_NODE_ADDON_API)
            Napi::Env napi_env_wrap(env);
            Napi::Object exports_object(napi_env_wrap, exports);
            exports_object["nodeAddonApiAdd"] =
                Napi::Function::New(napi_env_wrap, node_addon_api_add, "nodeAddonApiAdd");
            exports_object["nodeAddonApiCalc"] =
                Napi::Function::New(napi_env_wrap, node_addon_api_calc, "nodeAddonApiCalc");
            exports_object["nodeAddonApiCallLoop"] =
                Napi::Function::New(napi_env_wrap, node_addon_api_call_loop, "nodeAddonApiCallLoop");
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
