#define PORTAL_IMPORT_DEF "tests/import.def"
#define PORTAL_EXPORT_DEF "tests/export.def"
#include "portal/portal.h"
#include "portal/backends/napi.h"

#include <stdexcept>
#include <string>
#include <type_traits>

namespace portal {
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

namespace {
    using napi_context_t = portal::napi_backend::context_t;
    using napi_export_context_t = portal::napi_backend::export_context_t;

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

    struct transform_function {
        int operator()(int value) const { return value * 10; }
        int operator()(int value, unsigned extra) const {
            return value + static_cast<int>(extra);
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

    napi_value string_value(napi_env env, const std::string& value) {
        napi_value result = nullptr;
        napi_create_string_utf8(env, value.data(), value.size(), &result);
        return result;
    }

    std::string value_to_string(napi_env env, napi_value value) {
        std::size_t size = 0;
        if (napi_get_value_string_utf8(env, value, nullptr, 0, &size) != napi_ok) {
            return std::string();
        }
        std::string text(size + 1, '\0');
        std::size_t copied = 0;
        napi_get_value_string_utf8(env, value, &text[0], text.size(), &copied);
        text.resize(copied);
        return text;
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
        return portal::import_symbol_traits<Symbol>::symbol_name();
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

    napi_value echo_callback(napi_env env, napi_callback_info info) {
        std::size_t argc = 1;
        napi_value argv[1] = {};
        napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
        const std::string decorated = "[" + value_to_string(env, argv[0]) + "]";
        return string_value(env, decorated);
    }

    napi_value callback_value(napi_env env, napi_callback_info info) {
        std::size_t argc = 1;
        napi_value argv[1] = {};
        napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
        return int_value(env, value_to_int(env, argv[0]) * 3);
    }

    napi_value pass_counter_callback(napi_env env, napi_callback_info info) {
        std::size_t argc = 2;
        napi_value argv[2] = {};
        napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
        return int_value(env, object_handle(env, argv[0]) + value_to_int(env, argv[1]));
    }

    napi_value pass_transform_callback(napi_env env, napi_callback_info info) {
        std::size_t argc = 2;
        napi_value argv[2] = {};
        napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
        napi_value result = nullptr;
        napi_call_function(env, nullptr, argv[0], 1, &argv[1], &result);
        return result;
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
    !std::is_same<
        portal::import_symbols::counter::add,
        portal::interface_descriptors::counter_addable<portal::import_t>
            ::method_symbols_t::add>::value,
    "a concrete interface projection should bind its own receiver symbol");

static_assert(
    std::is_same<
        portal::import_symbol_traits<
            portal::import_symbols::counter::add>::receiver_symbol_t,
        portal::import_symbols::counter>::value,
    "implemented interface methods should regain the concrete receiver identity");

namespace {

int run_cleanup_lifetime_test() {
    bool context_alive = true;
    bool native_destroyed = false;
    struct tracked_context : napi_export_context_t {
        bool& alive;
        tracked_context(napi_env env, bool& alive_)
            : napi_export_context_t(env), alive(alive_) {}
        ~tracked_context() { alive = false; }
    };
    struct tracked_native {
        bool& context_alive;
        bool& destroyed;
        ~tracked_native() { destroyed = !context_alive; }
    };

    napi_env env = napi_stub_create_env();
    auto* ctx = new tracked_context(env, context_alive);
    napi_add_env_cleanup_hook(
        env, [](void* data) { delete static_cast<tracked_context*>(data); }, ctx);
    napi_value instance = nullptr;
    napi_create_object(env, &instance);
    {
        portal::napi_backend::object_t<tracked_native, portal::export_t> object;
        object.construct_export_object_from_native(
            *ctx, instance, new tracked_native{context_alive, native_destroyed});
    }
    napi_stub_delete_env(env);
    return !context_alive && native_destroyed ? 0 : 74;
}

int run_test(napi_env env) {
    {
        using target_t = portal::napi_backend::class_target_t;
        auto constructor = napi_stub_make_value(env, napi_value__::kind_t::function);
        napi_ref moved_ref = nullptr;
        {
            target_t first(env, constructor, nullptr);
            target_t second(env, constructor, nullptr);
            napi_ref old_ref = first.constructor_ref;
            moved_ref = second.constructor_ref;
            first = std::move(second);
            if (old_ref->value != nullptr || moved_ref->value != constructor) return 70;
            target_t moved(std::move(first));
            if (moved.constructor_ref != moved_ref || env->cleanup_hooks.size() != 1) return 71;
        }
        if (moved_ref->value != nullptr || !env->cleanup_hooks.empty()) return 72;

        // Static module contexts may be destroyed after the env. The cleanup
        // hook must make their later reference destruction harmless.
        napi_env temporary_env = napi_stub_create_env();
        auto temporary_constructor = napi_stub_make_value(
            temporary_env, napi_value__::kind_t::function);
        target_t late_target(temporary_env, temporary_constructor, nullptr);
        napi_stub_delete_env(temporary_env);
    }
    record_state state;
    napi_value record = nullptr;
    napi_create_function(env, "record", NAPI_AUTO_LENGTH, record_callback, &state, &record);

    napi_value import_module_value = nullptr;
    napi_create_object(env, &import_module_value);
    set_property(env, import_module_value, symbol_name<portal::import_symbols::foo>(), record);
    set_property(env, import_module_value, symbol_name<portal::import_symbols::bar>(), record);
    portal::napi_backend::module_t import_module{env, import_module_value};

    auto record_ctx = portal::import_from<portal::import_symbols::foo, napi_context_t>(
        import_module);
    portal::call_foo(record_ctx, 1, 2);
    if (state.argc != 2 || state.first != 1 || state.second != 2) {
        return 1;
    }

    auto bar_ctx = portal::import_from<portal::import_symbols::bar, napi_context_t>(
        import_module);
    portal::bar(bar_ctx)(7);
    if (state.argc != 1 || state.first != 7) {
        return 2;
    }

    napi_value calc = nullptr;
    napi_create_function(env, symbol_name<portal::import_symbols::calc>(),
        NAPI_AUTO_LENGTH, calc_callback, nullptr, &calc);
    set_property(env, import_module_value, symbol_name<portal::import_symbols::calc>(), calc);
    auto calc_ctx = portal::import_from<portal::import_symbols::calc, napi_context_t>(
        import_module);
    if (portal::call_calc(calc_ctx, 3, 4u) != 7) {
        return 3;
    }

    napi_value echo = nullptr;
    napi_create_function(env, symbol_name<portal::import_symbols::echo>(),
        NAPI_AUTO_LENGTH, echo_callback, nullptr, &echo);
    set_property(env, import_module_value, symbol_name<portal::import_symbols::echo>(), echo);
    auto echo_ctx = portal::import_from<portal::import_symbols::echo, napi_context_t>(
        import_module);
    const std::string utf8_text = "h\xC3\xA9llo \xE4\xB8\xAD";
    const std::string embedded_text(std::string("a\0b", 3) + utf8_text);
    if (portal::call_echo(echo_ctx, utf8_text) != "[" + utf8_text + "]"
            || portal::call_echo(echo_ctx, embedded_text) != "[" + embedded_text + "]") {
        return 60;
    }

    napi_value receiver = nullptr;
    napi_create_object(env, &receiver);
    set_property(env, receiver, "handle", int_value(env, 13));

    napi_value counter_call = nullptr;
    napi_create_function(env, "counter_call", NAPI_AUTO_LENGTH, counter_callback, nullptr, &counter_call);
    set_property(env, import_module_value, symbol_name<portal::import_symbols::counter>(), counter_call);
    portal::napi_backend::object_t<void> import_source(env, import_module_value);
    auto counter_ctx = portal::import_from<portal::import_symbols::counter, napi_context_t>(
        import_source);
    auto counter = portal::bind_receiver<portal::counter>(
        counter_ctx,
        env,
        receiver);
    if (counter.add(29) != 42 || counter.value() != 13) {
        return 4;
    }

    auto constructed_counter = portal::construct<portal::counter>(counter_ctx, 21u);
    if (constructed_counter.value() != 21 || constructed_counter.add(21) != 42) {
        return 8;
    }

    napi_value callback = nullptr;
    napi_value pass_counter = nullptr;
    napi_value pass_transform = nullptr;
    napi_create_function(env, "callback", NAPI_AUTO_LENGTH,
        callback_value, nullptr, &callback);
    napi_create_function(env, "pass_counter", NAPI_AUTO_LENGTH,
        pass_counter_callback, nullptr, &pass_counter);
    napi_create_function(env, "pass_transform", NAPI_AUTO_LENGTH,
        pass_transform_callback, nullptr, &pass_transform);
    set_property(env, import_module_value,
        symbol_name<portal::import_symbols::callback>(), callback);
    set_property(env, import_module_value,
        symbol_name<portal::import_symbols::pass_counter>(), pass_counter);
    set_property(env, import_module_value,
        symbol_name<portal::import_symbols::pass_transform>(), pass_transform);

    auto pass_counter_ctx = portal::import_from<
        portal::import_symbols::pass_counter, napi_context_t>(import_module);
    if (portal::call_pass_counter(pass_counter_ctx, counter, 29) != 42) {
        return 31;
    }
    auto pass_transform_ctx = portal::import_from<
        portal::import_symbols::pass_transform, napi_context_t>(import_module);
    if (portal::call_pass_transform(
            pass_transform_ctx, transform_function{}, 4) != 40) {
        return 32;
    }
    auto built_transform = portal::bind_transform()
        .bind<int(int)>(scale_by_ten_function)
        .bind<int(int, unsigned)>([](int value, unsigned extra) {
            return value * static_cast<int>(extra);
        })
        .build();
    if (portal::call_pass_transform(
            pass_transform_ctx, std::move(built_transform), 6) != 60) {
        return 33;
    }

    napi_value module_value = nullptr;
    napi_create_object(env, &module_value);
    portal::napi_backend::module_t module{env, module_value};
    napi_export_context_t export_ctx(env);

    napi_value bad_int = nullptr;
    napi_create_object(env, &bad_int);
    auto maybe_int = portal::from_optional<int>(export_ctx, bad_int);
    if (maybe_int) {
        return 24;
    }

    auto maybe_unsigned = portal::from_optional<unsigned>(export_ctx, int_value(env, -1));
    if (maybe_unsigned) {
        return 25;
    }

    auto maybe_string = portal::from_optional<std::string>(export_ctx, bad_int);
    if (maybe_string) {
        return 61;
    }
    const std::string embedded_probe(std::string("a\0b", 3));
    auto maybe_embedded = portal::from_optional<std::string>(
        export_ctx, string_value(env, embedded_probe));
    if (!maybe_embedded || maybe_embedded.value() != embedded_probe) {
        return 62;
    }

    bool caught_string_api_failure = false;
    try {
        (void)portal::from_optional<std::string>(export_ctx, nullptr);
    } catch (const std::runtime_error&) {
        caught_string_api_failure = true;
    }
    if (!caught_string_api_failure) {
        return 65;
    }

    bool caught_bad_conversion = false;
    try {
        (void)portal::from_cast<int>(export_ctx, bad_int);
    } catch (const portal::bad_conversion&) {
        caught_bad_conversion = true;
    }
    if (!caught_bad_conversion) {
        return 20;
    }

    portal::export_free_callable(export_ctx, module, "add", add_function);
    portal::export_calc(export_ctx, module, add_function);
    portal::export_free_callable(export_ctx, module, "store", store_function);
    portal::export_calc<int(int, unsigned)>(
        export_ctx,
        module,
        [](int a, unsigned b) {
            return a * static_cast<int>(b);
        });
    portal::export_calc(export_ctx, module)
        .bind<int(int)>(scale_by_ten_function)
        .bind<int(int, unsigned)>(multiply_function{})
        .commit();
    if (value_to_int(env, call1(env, get_property(env, module_value, "calc"), int_value(env, 6))) != 60
            || value_to_int(env, call2(env, get_property(env, module_value, "calc"), int_value(env, 6), int_value(env, 7))) != 42) {
        return 30;
    }

    portal::export_calc(export_ctx, module)
        .bind<int(int)>([](int a) {
            return a * 11;
        })
        .bind<int(int, unsigned)>([](int a, unsigned b) {
            return a * static_cast<int>(b) + 1;
        })
        .commit();

    portal::export_echo(export_ctx, module)
        .bind<std::string(std::string)>([](std::string text) {
            return "<" + text + ">";
        })
        .bind<int(int)>([](int value) {
            return value * 2;
        })
        .commit();
    napi_value echo_export = get_property(env, module_value, "echo");
    if (value_to_string(env, call1(env, echo_export, string_value(env, utf8_text)))
            != "<" + utf8_text + ">"
            || value_to_string(env, call1(env, echo_export, string_value(env, embedded_text)))
            != "<" + embedded_text + ">"
            || value_to_int(env, call1(env, echo_export, int_value(env, 21))) != 42) {
        return 63;
    }
    if (call1(env, echo_export, bad_int) != nullptr) {
        return 64;
    }

    auto exported_calc_ctx = portal::import_from<portal::import_symbols::calc, napi_context_t>(
        module);
    if (value_to_int(env, call2(env, get_property(env, module_value, "add"), int_value(env, 12), int_value(env, 13))) != 25
            || value_to_int(env, call1(env, get_property(env, module_value, "calc"), int_value(env, 6))) != 66
            || portal::call_calc(exported_calc_ctx, 6, 7u) != 43) {
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

    portal::exports::counter::register_all(export_ctx, module);

    auto counter_class_ctx = portal::import_from<portal::import_symbols::counter, napi_context_t>(
        module);
    napi_value counter_class = counter_class_ctx.callable();
    napi_value prototype = get_property(env, counter_class, "prototype");
    napi_value member_add = get_property(env, prototype,
        symbol_name<portal::import_symbols::counter::add>());
    napi_value member_value = get_property(env, prototype,
        symbol_name<portal::import_symbols::counter::value>());

    napi_value constructor_arg = int_value(env, 13);
    napi_value instance = nullptr;
    napi_new_instance(env, counter_class, 1, &constructor_arg, &instance);

    // Env-owned contexts are deleted by cleanup hooks before native object
    // finalizers run. A wrapped object must not retain a context as its hint.
    if (instance->finalizer == nullptr || instance->finalizer_hint != nullptr) {
        return 73;
    }

    if (value_to_int(env, call1(env, member_add, int_value(env, 29), instance)) != 42
            || value_to_int(env, call0(env, member_value, instance)) != 13) {
        return 11;
    }
    if (env->type_tag_checks == 0) {
        return 40;
    }

    portal::native::counter borrowed_counter(31u);
    auto borrowed_object = portal::make_exported<
        portal::exports::counter>(
        export_ctx,
        portal::borrow(borrowed_counter));
    if (value_to_int(env, call1(env, member_add, int_value(env, 11), borrowed_object.get())) != 42
            || value_to_int(env, call0(env, member_value, borrowed_object.get())) != 31) {
        return 22;
    }

    using consume_counter_sig = int(
        portal::object_param<portal::export_classes::counter, portal::export_t>,
        int);
    portal::export_consume_counter<consume_counter_sig>(
        export_ctx,
        module,
        [](portal::native::counter& source, int value) {
            return source.add(value);
        });
    napi_value consume_counter = get_property(env, module_value, "consume_counter");
    if (value_to_int(env, call2(
            env, consume_counter, borrowed_object.get(), int_value(env, 11))) != 42) {
        return 34;
    }
    if (call2(env, consume_counter, int_value(env, 7), int_value(env, 7)) != nullptr) {
        return 35;
    }

    using use_callback_sig = int(
        portal::callable_param<portal::import_symbols::callback, portal::import_t>,
        int);
    portal::export_use_callback<use_callback_sig>(
        export_ctx,
        module,
        [](auto& callback_ctx, int value) {
            return portal::call_callback(callback_ctx, value) + 1;
        });
    napi_value use_callback = get_property(env, module_value, "use_callback");
    if (value_to_int(env, call2(
            env, use_callback, callback, int_value(env, 7))) != 22) {
        return 36;
    }
    if (call2(env, use_callback, int_value(env, 7), int_value(env, 7)) != nullptr) {
        return 37;
    }

    portal::exports::consumer::register_all(export_ctx, module);
    napi_value consumer_class = get_property(env, module_value, "consumer");
    napi_value consumer_args[] = {borrowed_object.get(), callback};
    napi_value consumer_instance = nullptr;
    napi_new_instance(env, consumer_class, 2, consumer_args, &consumer_instance);
    napi_value consumer_prototype = get_property(env, consumer_class, "prototype");
    napi_value combine = get_property(env, consumer_prototype, "combine");
    napi_value apply = get_property(env, consumer_prototype, "apply");
    if (value_to_int(env, call2(
            env, combine, borrowed_object.get(), int_value(env, 11), consumer_instance)) != 135
            || value_to_int(env, call2(
                env, apply, callback, int_value(env, 7), consumer_instance)) != 114) {
        return 38;
    }
    if (call2(env, consume_counter, consumer_instance, int_value(env, 1)) != nullptr
            || call1(env, member_add, int_value(env, 1), consumer_instance) != nullptr) {
        return 39;
    }

    portal::export_instance<
        portal::exports::counter>(
        export_ctx,
        module,
        "globalCounter",
        portal::borrow(borrowed_counter));
    napi_value global_counter = get_property(env, module_value, "globalCounter");
    if (value_to_int(env, call1(env, member_add, int_value(env, 11), global_counter)) != 42) {
        return 23;
    }

    napi_value trusted_module_value = nullptr;
    napi_create_object(env, &trusted_module_value);
    portal::napi_backend::module_t trusted_module{env, trusted_module_value};
    portal::napi_backend::trusted_export_context_t trusted_ctx(env);
    portal::exports::counter::register_all(trusted_ctx, trusted_module);

    napi_value trusted_counter_class = get_property(env, trusted_module_value, "counter");
    napi_value trusted_constructor_arg = int_value(env, 17);
    napi_value trusted_instance = nullptr;
    napi_new_instance(
        env, trusted_counter_class, 1, &trusted_constructor_arg, &trusted_instance);
    napi_value trusted_prototype = get_property(env, trusted_counter_class, "prototype");
    napi_value trusted_add = get_property(env, trusted_prototype, "add");
    const std::size_t checks_before_trusted_call = env->type_tag_checks;
    if (value_to_int(env, call1(
            env, trusted_add, int_value(env, 25), trusted_instance)) != 42
            || env->type_tag_checks != checks_before_trusted_call) {
        return 41;
    }

    return 0;
}

}  // namespace

int main() {
    const int lifetime_result = run_cleanup_lifetime_test();
    if (lifetime_result != 0) return lifetime_result;
    napi_env env = napi_stub_create_env();
    // Lifetime contract: every portal wrapper/context must be destroyed
    // before the stub env. Their destructors call napi_delete_reference on
    // refs owned by env->refs; deleting the env first frees those refs and
    // turns every later reset() into a use-after-free (MSVC Debug heap
    // catches it; Linux/GCC silently tolerates the writes).
    const int result = run_test(env);
    napi_stub_delete_env(env);
    return result;
}
