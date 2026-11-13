#define PORTAL_IMPORT_DEF "tests/import.def"
#define PORTAL_EXPORT_DEF "tests/export.def"
#include "portal/portal.h"
#include "portal/backends/napi.h"

#include <exception>
#include <memory>
#include <stdexcept>
#include <string>

using napi_context_t = portal::napi_backend::context_t;
using napi_export_context_t = portal::napi_backend::export_context_t;

namespace portal {
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
                : base_(portal::call_callback(callback, source.value())) {
            }

            int combine(counter& source, int value) const {
                return base_ + source.value() + value;
            }

            template <typename Callback>
            int apply(Callback& callback, int value) const {
                return base_ + portal::call_callback(callback, value);
            }

        private:
            int base_ = 0;
        };
    }
}

int portal::native::counter::constructed = 0;
int portal::native::counter::destroyed = 0;

using owned_counter = portal::native::counter;

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
            const int a = portal::from_cast<int>(ctx, argv[1]);
            const unsigned b = portal::from_cast<unsigned>(ctx, argv[2]);
            return portal::napi_backend::converter<int>::to(
                ctx,
                portal::call_calc(ctx, a, b));
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
            const int a = portal::from_cast<int>(ctx, argv[1]);
            const int b = portal::from_cast<int>(ctx, argv[2]);
            portal::call_foo(ctx, a, b);

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
            const std::string text = portal::from_cast<std::string>(ctx, argv[1]);
            return portal::napi_backend::converter<std::string>::to(
                ctx,
                portal::call_echo(ctx, text));
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
            auto counter = portal::bind_receiver<portal::counter>(
                ctx,
                env,
                argv[1]);
            const int value = portal::from_cast<int>(ctx, argv[2]);
            return portal::napi_backend::converter<int>::to(ctx, counter.add(value));
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
            auto counter = portal::bind_receiver<portal::counter>(
                ctx,
                env,
                argv[1]);
            return portal::napi_backend::converter<int>::to(ctx, counter.value());
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
            const unsigned handle = portal::from_cast<unsigned>(ctx, argv[1]);
            const int value = portal::from_cast<int>(ctx, argv[2]);
            auto counter = portal::construct<portal::counter>(ctx, handle);
            return portal::napi_backend::converter<int>::to(ctx, counter.add(value));
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
            auto counter = portal::bind_receiver<portal::counter>(ctx, env, argv[1]);
            const int value = portal::from_cast<int>(ctx, argv[2]);
            return portal::napi_backend::converter<int>::to(
                ctx, portal::call_pass_counter(ctx, counter, value));
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
            const int value = portal::from_cast<int>(ctx, argv[1]);
            return portal::napi_backend::converter<int>::to(
                ctx, portal::call_pass_transform(ctx, transform_function{}, value));
        } catch (const std::exception& error) {
            return throw_error(env, error.what());
        }
    }

    void define_function(
        napi_env env,
        portal::napi_backend::module_t& module,
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
            std::unique_ptr<napi_export_context_t> owner(new napi_export_context_t(env));
            portal::napi_backend::check(napi_add_env_cleanup_hook(
                env, [](void* data) { delete static_cast<napi_export_context_t*>(data); }, owner.get()),
                "napi_add_env_cleanup_hook failed for export context");
            auto& ctx = *owner.release();
            portal::napi_backend::module_t module{env, exports};

            portal::export_free_callable(ctx, module, "add", add_function);
            portal::export_calc(ctx, module, add_function);
            portal::export_free_callable(ctx, module, "store", store_function);
            portal::export_free_callable(ctx, module, "stored", stored_function);
            portal::export_free_callable(ctx, module, "ownedCounterConstructed", owned_counter_constructed);
            portal::export_free_callable(ctx, module, "ownedCounterDestroyed", owned_counter_destroyed);
            portal::export_free_callable<int(int)>(
                ctx, module, "identityInt", [](int value) { return value; });
            portal::export_free_callable<unsigned(unsigned)>(
                ctx, module, "identityUnsigned", [](unsigned value) { return value; });
            portal::export_free_callable<int(int, unsigned)>(
                ctx,
                module,
                "multiply",
                [](int a, unsigned b) {
                    return a * static_cast<int>(b);
                });
            portal::export_calc(ctx, module)
                .bind<int(int)>(scale_by_ten_function)
                .bind<int(int, unsigned)>(multiply_function{})
                .commit();
            portal::export_echo(ctx, module)
                .bind<std::string(std::string)>([](std::string text) {
                    return "<" + text + ">";
                })
                .bind<int(int)>([](int value) {
                    return value * 2;
                })
                .commit();

            portal::exports::counter::register_all(ctx, module);
            using consume_counter_sig = int(
                portal::object_param<portal::export_classes::counter, portal::export_t>,
                int);
            portal::export_consume_counter<consume_counter_sig>(
                ctx,
                module,
                [](owned_counter& counter, int value) {
                    return counter.add(value);
                });
            using use_callback_sig = int(
                portal::callable_param<portal::import_symbols::callback, portal::import_t>,
                int);
            portal::export_use_callback<use_callback_sig>(
                ctx,
                module,
                [](auto& callback, int value) {
                    return portal::call_callback(callback, value) + 1;
                });
            portal::exports::consumer::register_all(ctx, module);

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

NAPI_MODULE(portal_napi_runtime_addon, init)
