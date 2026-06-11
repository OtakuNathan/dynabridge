#define DYNABRIDGE_IMPORT_DEF "tests/import.def"
#define DYNABRIDGE_EXPORT_DEF "tests/export.def"
#include "dynabridge/bridge.h"
#include "dynabridge/backends/napi.h"

#include <exception>
#include <stdexcept>

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
    }
}

int dynabridge::native::counter::constructed = 0;
int dynabridge::native::counter::destroyed = 0;

using owned_counter = dynabridge::native::counter;

template <>
struct dynabridge::napi_backend::converter<dynabridge::counter<napi_context_t>> {
    static napi_value to(context_t&, dynabridge::counter<napi_context_t>& counter) {
        return counter.object().get();
    }

    static dynabridge::optional<dynabridge::counter<napi_context_t>> from(context_t& ctx, napi_value value) {
        return dynabridge::optional<dynabridge::counter<napi_context_t>>(dynabridge::counter<napi_context_t>(
            ctx,
            object_t<dynabridge::counter<napi_context_t>>(ctx.env(), value)));
    }
};

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

            dynabridge::exports::counter<dynabridge::native::counter>::register_all(ctx, module);

            define_function(env, module, "callImportedCalc", call_imported_calc);
            define_function(env, module, "callImportedFoo", call_imported_foo);
            define_function(env, module, "callImportedCounterAdd", call_imported_counter_add);
            define_function(env, module, "callImportedCounterValue", call_imported_counter_value);
            define_function(env, module, "constructImportedCounterAdd", construct_imported_counter_add);

            return exports;
        } catch (const std::exception& error) {
            return throw_error(env, error.what());
        }
    }
}

NAPI_MODULE(dynabridge_napi_runtime_addon, init)
