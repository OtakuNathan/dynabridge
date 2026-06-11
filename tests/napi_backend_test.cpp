#define DYNABRIDGE_IMPORT_DEF "tests/import.def"
#define DYNABRIDGE_EXPORT_DEF "tests/export.def"
#include "dynabridge/bridge.h"
#include "dynabridge/backends/napi.h"

#include <type_traits>

namespace dynabridge {
    namespace native {
        class counter {
        public:
            explicit counter(unsigned initial_handle)
                : handle(static_cast<int>(initial_handle)) {
            }

            int add(int value) const noexcept {
                return handle + value;
            }

            int value() const noexcept {
                return handle;
            }

        private:
            int handle = 0;
        };
    }
}

namespace {
    using napi_context_t = dynabridge::napi_backend::context_t;
    using napi_export_context_t = dynabridge::napi_backend::export_context_t;

    struct record_state {
        int argc = 0;
        int first = 0;
        int second = 0;
    };

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

    int value_to_int(napi_env env, napi_value value) {
        int result = 0;
        napi_get_value_int32(env, value, &result);
        return result;
    }

    napi_value int_value(napi_env env, int value) {
        napi_value result = nullptr;
        napi_create_int32(env, value, &result);
        return result;
    }

    napi_value get_property(napi_env env, napi_value object, const char* name) {
        napi_value result = nullptr;
        napi_get_named_property(env, object, name, &result);
        return result;
    }

    void set_property(napi_env env, napi_value object, const char* name, napi_value value) {
        napi_set_named_property(env, object, name, value);
    }

    template <typename Symbol>
    const char* symbol_name() noexcept {
        return dynabridge::import_symbol_traits<Symbol>::symbol_name();
    }

    int object_handle(napi_env env, napi_value object) {
        return value_to_int(env, get_property(env, object, "handle"));
    }

    napi_value record_callback(napi_env env, napi_callback_info info) {
        void* data = nullptr;
        std::size_t argc = 2;
        napi_value argv[2] = {};
        napi_get_cb_info(env, info, &argc, argv, nullptr, &data);

        record_state* state = static_cast<record_state*>(data);
        state->argc = static_cast<int>(argc);
        state->first = argc > 0 ? value_to_int(env, argv[0]) : 0;
        state->second = argc > 1 ? value_to_int(env, argv[1]) : 0;

        napi_value undefined = nullptr;
        napi_get_undefined(env, &undefined);
        return undefined;
    }

    napi_value calc_callback(napi_env env, napi_callback_info info) {
        std::size_t argc = 2;
        napi_value argv[2] = {};
        napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
        return int_value(env, value_to_int(env, argv[0]) + value_to_int(env, argv[1]));
    }

    napi_value counter_callback(napi_env env, napi_callback_info info) {
        std::size_t argc = 2;
        napi_value argv[2] = {};
        napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);

        napi_valuetype first_type = napi_undefined;
        napi_typeof(env, argv[0], &first_type);
        if (argc == 1 && first_type == napi_number) {
            napi_value object = nullptr;
            napi_create_object(env, &object);
            set_property(env, object, "handle", argv[0]);
            return object;
        }

        const int handle = object_handle(env, argv[0]);
        if (argc == 1) {
            return int_value(env, handle);
        }
        return int_value(env, handle + value_to_int(env, argv[1]));
    }

    napi_value call0(napi_env env, napi_value function, napi_value receiver = nullptr) {
        napi_value result = nullptr;
        napi_call_function(env, receiver, function, 0, nullptr, &result);
        return result;
    }

    napi_value call1(napi_env env, napi_value function, napi_value a, napi_value receiver = nullptr) {
        napi_value argv[] = {a};
        napi_value result = nullptr;
        napi_call_function(env, receiver, function, 1, argv, &result);
        return result;
    }

    napi_value call2(
        napi_env env,
        napi_value function,
        napi_value a,
        napi_value b,
        napi_value receiver = nullptr)
    {
        napi_value argv[] = {a, b};
        napi_value result = nullptr;
        napi_call_function(env, receiver, function, 2, argv, &result);
        return result;
    }
}

static_assert(
    std::is_same<
        dynabridge::import_symbol_traits<dynabridge::import_symbols::counter::add>::receiver_symbol_t,
        dynabridge::import_symbols::counter>::value,
    "member import symbol should retain its receiver symbol");

static_assert(
    std::is_same<
        dynabridge::import_symbol_traits<dynabridge::import_symbols::counter::value>::receiver_symbol_t,
        dynabridge::import_symbols::counter>::value,
    "member import symbol should retain its receiver symbol");

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

int main() {
    napi_env env = napi_stub_create_env();

    record_state state;
    napi_value record = nullptr;
    napi_create_function(env, "record", NAPI_AUTO_LENGTH, record_callback, &state, &record);

    napi_value import_module_value = nullptr;
    napi_create_object(env, &import_module_value);
    set_property(env, import_module_value, symbol_name<dynabridge::import_symbols::foo>(), record);
    set_property(env, import_module_value, symbol_name<dynabridge::import_symbols::bar>(), record);
    dynabridge::napi_backend::module_t import_module{env, import_module_value};

    auto record_ctx = dynabridge::import_from<dynabridge::import_symbols::foo, napi_context_t>(
        import_module);
    dynabridge::call_foo(record_ctx, 1, 2);
    if (state.argc != 2 || state.first != 1 || state.second != 2) {
        return 1;
    }

    auto bar_ctx = dynabridge::import_from<dynabridge::import_symbols::bar, napi_context_t>(
        import_module);
    dynabridge::bar(bar_ctx)(7);
    if (state.argc != 1 || state.first != 7) {
        return 2;
    }

    napi_value calc = nullptr;
    napi_create_function(env, symbol_name<dynabridge::import_symbols::calc>(),
        NAPI_AUTO_LENGTH, calc_callback, nullptr, &calc);
    set_property(env, import_module_value, symbol_name<dynabridge::import_symbols::calc>(), calc);
    auto calc_ctx = dynabridge::import_from<dynabridge::import_symbols::calc, napi_context_t>(
        import_module);
    if (dynabridge::call_calc(calc_ctx, 3, 4u) != 7) {
        return 3;
    }

    napi_value receiver = nullptr;
    napi_create_object(env, &receiver);
    set_property(env, receiver, "handle", int_value(env, 13));

    napi_value counter_call = nullptr;
    napi_create_function(env, "counter_call", NAPI_AUTO_LENGTH, counter_callback, nullptr, &counter_call);
    set_property(env, import_module_value, symbol_name<dynabridge::import_symbols::counter>(), counter_call);
    dynabridge::napi_backend::object_t<void> import_source(env, import_module_value);
    auto counter_ctx = dynabridge::import_from<dynabridge::import_symbols::counter, napi_context_t>(
        import_source);
    auto counter = dynabridge::bind_receiver<dynabridge::counter>(
        counter_ctx,
        env,
        receiver);
    if (counter.add(29) != 42 || counter.value() != 13) {
        return 4;
    }

    auto constructed_counter = dynabridge::construct<dynabridge::counter>(counter_ctx, 21u);
    if (constructed_counter.value() != 21 || constructed_counter.add(21) != 42) {
        return 8;
    }

    napi_value module_value = nullptr;
    napi_create_object(env, &module_value);
    dynabridge::napi_backend::module_t module{env, module_value};
    napi_export_context_t export_ctx(env);

    napi_value bad_int = nullptr;
    napi_create_object(env, &bad_int);
    auto maybe_int = dynabridge::from_optional<int>(export_ctx, bad_int);
    if (maybe_int) {
        return 24;
    }

    auto maybe_unsigned = dynabridge::from_optional<unsigned>(export_ctx, int_value(env, -1));
    if (maybe_unsigned) {
        return 25;
    }

    bool caught_bad_conversion = false;
    try {
        (void)dynabridge::from_cast<int>(export_ctx, bad_int);
    } catch (const dynabridge::bad_conversion&) {
        caught_bad_conversion = true;
    }
    if (!caught_bad_conversion) {
        return 20;
    }

    dynabridge::export_free_callable(export_ctx, module, "add", add_function);
    dynabridge::export_calc(export_ctx, module, add_function);
    dynabridge::export_free_callable(export_ctx, module, "store", store_function);
    dynabridge::export_calc<int(int, unsigned)>(
        export_ctx,
        module,
        [](int a, unsigned b) {
            return a * static_cast<int>(b);
        });
    dynabridge::export_calc(export_ctx, module)
        .bind<int(int)>(scale_by_ten_function)
        .bind<int(int, unsigned)>(multiply_function{})
        .commit();
    if (value_to_int(env, call1(env, get_property(env, module_value, "calc"), int_value(env, 6))) != 60
            || value_to_int(env, call2(env, get_property(env, module_value, "calc"), int_value(env, 6), int_value(env, 7))) != 42) {
        return 30;
    }

    dynabridge::export_calc(export_ctx, module)
        .bind<int(int)>([](int a) {
            return a * 11;
        })
        .bind<int(int, unsigned)>([](int a, unsigned b) {
            return a * static_cast<int>(b) + 1;
        })
        .commit();

    auto exported_calc_ctx = dynabridge::import_from<dynabridge::import_symbols::calc, napi_context_t>(
        module);
    if (value_to_int(env, call2(env, get_property(env, module_value, "add"), int_value(env, 12), int_value(env, 13))) != 25
            || value_to_int(env, call1(env, get_property(env, module_value, "calc"), int_value(env, 6))) != 66
            || dynabridge::call_calc(exported_calc_ctx, 6, 7u) != 43) {
        return 9;
    }
    if (call2(env, get_property(env, module_value, "add"), bad_int, int_value(env, 1)) != nullptr) {
        return 21;
    }

    stored_value = 0;
    call1(env, get_property(env, module_value, "store"), int_value(env, 77));
    if (stored_value != 77) {
        return 10;
    }

    dynabridge::exports::counter<dynabridge::native::counter>::register_all(export_ctx, module);

    auto counter_class_ctx = dynabridge::import_from<dynabridge::import_symbols::counter, napi_context_t>(
        module);
    napi_value counter_class = counter_class_ctx.callable();
    napi_value prototype = get_property(env, counter_class, "prototype");
    napi_value member_add = get_property(env, prototype,
        symbol_name<dynabridge::import_symbols::counter::add>());
    napi_value member_value = get_property(env, prototype,
        symbol_name<dynabridge::import_symbols::counter::value>());

    napi_value constructor_arg = int_value(env, 13);
    napi_value instance = nullptr;
    napi_new_instance(env, counter_class, 1, &constructor_arg, &instance);

    if (value_to_int(env, call1(env, member_add, int_value(env, 29), instance)) != 42
            || value_to_int(env, call0(env, member_value, instance)) != 13) {
        return 11;
    }

    dynabridge::native::counter borrowed_counter(31u);
    auto borrowed_object = dynabridge::make_exported<
        dynabridge::exports::counter<dynabridge::native::counter>>(
        export_ctx,
        dynabridge::borrow(borrowed_counter));
    if (value_to_int(env, call1(env, member_add, int_value(env, 11), borrowed_object.get())) != 42
            || value_to_int(env, call0(env, member_value, borrowed_object.get())) != 31) {
        return 22;
    }

    dynabridge::export_instance<
        dynabridge::exports::counter<dynabridge::native::counter>>(
        export_ctx,
        module,
        "globalCounter",
        dynabridge::borrow(borrowed_counter));
    napi_value global_counter = get_property(env, module_value, "globalCounter");
    if (value_to_int(env, call1(env, member_add, int_value(env, 11), global_counter)) != 42) {
        return 23;
    }

    napi_stub_delete_env(env);
    return 0;
}
