#define DYNABRIDGE_IMPORT_DEF "tests/import.def"
#define DYNABRIDGE_EXPORT_DEF "tests/export.def"
#include "dynabridge/bridge.h"
#include "dynabridge/backends/napi.h"

#include <exception>
#include <stdexcept>
#include <string>

using napi_context_t = dynabridge::napi_backend::context_t;
using napi_export_context_t = dynabridge::napi_backend::export_context_t;

namespace dynabridge {
    namespace native {
        class counter {
        public:
            static int constructed;
            static int destroyed;

            int handle = 13;

            explicit counter(unsigned initial_handle)
                : handle(static_cast<int>(initial_handle)) {
                ++constructed;
            }

            ~counter() {
                ++destroyed;
            }

            int add(int value) const noexcept {
                return handle + value;
            }

            int value() const noexcept {
                return handle;
            }
        };

        class consumer {
        public:
            template <typename Callback>
            consumer(counter& source, Callback& callback)
                : base_(dynabridge::call_callback(callback, source.value())) {
            }

            int combine(counter& source, int value) const {
                return base_ + source.value() + value;
            }

            template <typename Callback>
            int apply(Callback& callback, int value) const {
                return base_ + dynabridge::call_callback(callback, value);
            }

        private:
            int base_ = 0;
        };
    }
}

int dynabridge::native::counter::constructed = 0;
int dynabridge::native::counter::destroyed = 0;

using owned_counter = dynabridge::native::counter;

namespace {
    int stored_value = 0;

    int add_function(int a, unsigned b) {
        return a + static_cast<int>(b);
    }

    int scale_by_ten_function(int value) {
        return value * 10;
    }

    void store_function(int value) {
        stored_value = value;
    }

    struct multiply_function {
        int operator()(int a, unsigned b) const {
            return a * static_cast<int>(b);
        }
    };

    struct transform_function {
        int operator()(int value) const { return value * 10; }
        int operator()(int value, unsigned extra) const {
            return value + static_cast<int>(extra);
        }
    };

    int stored_function() {
        return stored_value;
    }

    int owned_counter_constructed() {
        return owned_counter::constructed;
    }

    int owned_counter_destroyed() {
        return owned_counter::destroyed;
    }

    napi_value throw_error(napi_env env, const char* message) {
        napi_throw_error(env, nullptr, message);
        return nullptr;
    }

    napi_value call_imported_calc(napi_env env, napi_callback_info info) {
        try {
            std::size_t argc = 3;
            napi_value argv[3] = {};
            if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc != 3) {
                return throw_error(env, "callImportedCalc expects callback, int, unsigned");
            }

            napi_context_t ctx(env, argv[0]);
            const int a = dynabridge::from_cast<int>(ctx, argv[1]);
            const unsigned b = dynabridge::from_cast<unsigned>(ctx, argv[2]);
            return dynabridge::napi_backend::converter<int>::to(
                ctx,
                dynabridge::call_calc(ctx, a, b));
        } catch (const std::exception& error) {
            return throw_error(env, error.what());
        }
    }

    napi_value call_imported_foo(napi_env env, napi_callback_info info) {
        try {
            std::size_t argc = 3;
            napi_value argv[3] = {};
            if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc != 3) {
                return throw_error(env, "callImportedFoo expects callback, int, int");
            }

            napi_context_t ctx(env, argv[0]);
            const int a = dynabridge::from_cast<int>(ctx, argv[1]);
            const int b = dynabridge::from_cast<int>(ctx, argv[2]);
            dynabridge::call_foo(ctx, a, b);

            napi_value undefined = nullptr;
            napi_get_undefined(env, &undefined);
            return undefined;
        } catch (const std::exception& error) {
            return throw_error(env, error.what());
        }
    }

    napi_value call_imported_echo(napi_env env, napi_callback_info info) {
        try {
            std::size_t argc = 2;
            napi_value argv[2] = {};
            if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc != 2) {
                return throw_error(env, "callImportedEcho expects callback, string");
            }

            napi_context_t ctx(env, argv[0]);
            const std::string text = dynabridge::from_cast<std::string>(ctx, argv[1]);
            return dynabridge::napi_backend::converter<std::string>::to(
                ctx,
                dynabridge::call_echo(ctx, text));
        } catch (const std::exception& error) {
            return throw_error(env, error.what());
        }
    }

    napi_value call_imported_counter_add(napi_env env, napi_callback_info info) {
        try {
            std::size_t argc = 3;
            napi_value argv[3] = {};
            if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc != 3) {
                return throw_error(env, "callImportedCounterAdd expects callback, object, int");
            }

            napi_context_t ctx(env, argv[0]);
            auto counter = dynabridge::bind_receiver<dynabridge::counter>(
                ctx,
                env,
                argv[1]);
            const int value = dynabridge::from_cast<int>(ctx, argv[2]);
            return dynabridge::napi_backend::converter<int>::to(ctx, counter.add(value));
        } catch (const std::exception& error) {
            return throw_error(env, error.what());
        }
    }

    napi_value call_imported_counter_value(napi_env env, napi_callback_info info) {
        try {
            std::size_t argc = 2;
            napi_value argv[2] = {};
            if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc != 2) {
                return throw_error(env, "callImportedCounterValue expects callback, object");
            }

            napi_context_t ctx(env, argv[0]);
            auto counter = dynabridge::bind_receiver<dynabridge::counter>(
                ctx,
                env,
                argv[1]);
            return dynabridge::napi_backend::converter<int>::to(ctx, counter.value());
        } catch (const std::exception& error) {
            return throw_error(env, error.what());
        }
    }

    napi_value construct_imported_counter_add(napi_env env, napi_callback_info info) {
        try {
            std::size_t argc = 3;
            napi_value argv[3] = {};
            if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc != 3) {
                return throw_error(env, "constructImportedCounterAdd expects callback, unsigned, int");
            }

            napi_context_t ctx(env, argv[0]);
            const unsigned handle = dynabridge::from_cast<unsigned>(ctx, argv[1]);
            const int value = dynabridge::from_cast<int>(ctx, argv[2]);
            auto counter = dynabridge::construct<dynabridge::counter>(ctx, handle);
            return dynabridge::napi_backend::converter<int>::to(ctx, counter.add(value));
        } catch (const std::exception& error) {
            return throw_error(env, error.what());
        }
    }

    napi_value call_imported_pass_counter(napi_env env, napi_callback_info info) {
        try {
            std::size_t argc = 3;
            napi_value argv[3] = {};
            if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc != 3) {
                return throw_error(env, "callImportedPassCounter expects callback, object, int");
            }
            napi_context_t ctx(env, argv[0]);
            auto counter = dynabridge::bind_receiver<dynabridge::counter>(ctx, env, argv[1]);
            const int value = dynabridge::from_cast<int>(ctx, argv[2]);
            return dynabridge::napi_backend::converter<int>::to(
                ctx, dynabridge::call_pass_counter(ctx, counter, value));
        } catch (const std::exception& error) {
            return throw_error(env, error.what());
        }
    }

    napi_value call_imported_pass_transform(napi_env env, napi_callback_info info) {
        try {
            std::size_t argc = 2;
            napi_value argv[2] = {};
            if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc != 2) {
                return throw_error(env, "callImportedPassTransform expects callback, int");
            }
            napi_context_t ctx(env, argv[0]);
            const int value = dynabridge::from_cast<int>(ctx, argv[1]);
            return dynabridge::napi_backend::converter<int>::to(
                ctx, dynabridge::call_pass_transform(ctx, transform_function{}, value));
        } catch (const std::exception& error) {
            return throw_error(env, error.what());
        }
    }

    void define_function(
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
            static napi_export_context_t ctx;
            ctx = napi_export_context_t(env);
            dynabridge::napi_backend::module_t module{env, exports};

            dynabridge::export_free_callable(ctx, module, "add", add_function);
            dynabridge::export_calc(ctx, module, add_function);
            dynabridge::export_free_callable(ctx, module, "store", store_function);
            dynabridge::export_free_callable(ctx, module, "stored", stored_function);
            dynabridge::export_free_callable(ctx, module, "ownedCounterConstructed", owned_counter_constructed);
            dynabridge::export_free_callable(ctx, module, "ownedCounterDestroyed", owned_counter_destroyed);
            dynabridge::export_free_callable<int(int, unsigned)>(
                ctx,
                module,
                "multiply",
                [](int a, unsigned b) {
                    return a * static_cast<int>(b);
                });
            dynabridge::export_calc(ctx, module)
                .bind<int(int)>(scale_by_ten_function)
                .bind<int(int, unsigned)>(multiply_function{})
                .commit();
            dynabridge::export_echo(ctx, module)
                .bind<std::string(std::string)>([](std::string text) {
                    return "<" + text + ">";
                })
                .bind<int(int)>([](int value) {
                    return value * 2;
                })
                .commit();

            dynabridge::exports::counter::register_all(ctx, module);
            using consume_counter_sig = int(
                dynabridge::object_param<dynabridge::export_classes::counter, dynabridge::export_t>,
                int);
            dynabridge::export_consume_counter<consume_counter_sig>(
                ctx,
                module,
                [](owned_counter& counter, int value) {
                    return counter.add(value);
                });
            using use_callback_sig = int(
                dynabridge::callable_param<dynabridge::import_symbols::callback, dynabridge::import_t>,
                int);
            dynabridge::export_use_callback<use_callback_sig>(
                ctx,
                module,
                [](auto& callback, int value) {
                    return dynabridge::call_callback(callback, value) + 1;
                });
            dynabridge::exports::consumer::register_all(ctx, module);

            define_function(env, module, "callImportedCalc", call_imported_calc);
            define_function(env, module, "callImportedFoo", call_imported_foo);
            define_function(env, module, "callImportedEcho", call_imported_echo);
            define_function(env, module, "callImportedCounterAdd", call_imported_counter_add);
            define_function(env, module, "callImportedCounterValue", call_imported_counter_value);
            define_function(env, module, "constructImportedCounterAdd", construct_imported_counter_add);
            define_function(env, module, "callImportedPassCounter", call_imported_pass_counter);
            define_function(env, module, "callImportedPassTransform", call_imported_pass_transform);

            return exports;
        } catch (const std::exception& error) {
            return throw_error(env, error.what());
        }
    }
}

NAPI_MODULE(dynabridge_napi_runtime_addon, init)
