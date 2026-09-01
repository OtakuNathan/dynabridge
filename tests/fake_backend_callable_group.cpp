#include <cstring>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#define DYNABRIDGE_IMPORT_DEF "tests/import.def"
#define DYNABRIDGE_EXPORT_DEF "tests/export.def"
#include "dynabridge/bridge.h"
#include "fake_backend.h"

namespace dynabridge {
    namespace native {
        class counter {
        public:
            explicit counter(unsigned initial_handle)
                : handle(initial_handle) {
            }

            int add(int value) {
                return static_cast<int>(handle) + value;
            }

            int value() const {
                return static_cast<int>(handle);
            }

        private:
            unsigned handle = 0;
        };
    }
}

namespace {
    int stored_value = 0;

    struct counter_handle {
        unsigned value = 0;
    };

    using native_counter = dynabridge::native::counter;

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
        int operator()(int value) const {
            return value * 10;
        }

        int operator()(int value, unsigned extra) const {
            return value + static_cast<int>(extra);
        }
    };

    struct consumer_runtime {
        unsigned operator()(unsigned object, int callback) const {
            return object + static_cast<unsigned>(
                dynabridge::fake_backend::invoke_dynamic_callable(callback, 1));
        }

        int operator()(unsigned receiver, unsigned object, int value) const {
            return static_cast<int>(receiver + object) + value;
        }

        int operator()(unsigned receiver, int callback, int value) const {
            return static_cast<int>(receiver)
                + dynabridge::fake_backend::invoke_dynamic_callable(callback, value);
        }
    };

    struct select_overload_function {
        int operator()(unsigned value) const {
            return 1000 + static_cast<int>(value);
        }

        int operator()(int value) const {
            return value * 10;
        }
    };

    void ref_store_function(int& value) {
        stored_value = value;
    }

    struct fake_module {
        int def_count = 0;
        int class_count = 0;
        const char* last_class_name = nullptr;
        const char* last_member_class_name = nullptr;
        std::function<int(int, unsigned)> add;
        std::function<int(int, unsigned)> calc;
        std::function<int(int)> calc_unary;
        std::function<int(int, unsigned)> explicit_add;
        std::function<int(int, unsigned)> lambda_add;
        std::function<int(unsigned, int)> counter_add;
        std::function<int(unsigned)> counter_value;
        unsigned exported_counter = 0;
        std::shared_ptr<void> exported_counter_lifetime;
        std::function<void(int)> store;

        template <typename>
        struct class_target {
            fake_module* module = nullptr;
            const char* class_name = nullptr;

            template <typename Binder>
            void def(const char* name, Binder binder) {
                module->last_member_class_name = class_name;
                module->def(name, std::move(binder));
            }

            template <typename, typename>
            void def_constructor() {
                ++module->def_count;
            }
        };

        template <typename Receiver>
        class_target<Receiver> def_class(const char* name) {
            ++class_count;
            last_class_name = name;
            return class_target<Receiver>{this, name};
        }

        template <typename Object>
        void def_instance(const char* name, Object object) {
            ++def_count;
            if (std::strcmp(name, "global_counter") == 0) {
                exported_counter = object.get();
                exported_counter_lifetime = std::make_shared<Object>(std::move(object));
            }
        }

        template <typename Binder,
            std::enable_if_t<!dynabridge::is_export_overload_binder<
                typename std::decay<Binder>::type>::value>* = nullptr>
        void def(const char* name, Binder binder) {
            def_impl(name, std::move(binder), dynabridge::type_identity<typename Binder::signature_t>{});
        }

        template <typename Binder,
            std::enable_if_t<dynabridge::is_export_overload_binder<
                typename std::decay<Binder>::type>::value>* = nullptr>
        void def(const char* name, Binder binder) {
            ++def_count;

            if (std::strcmp(name, "calc") == 0) {
                auto shared = std::make_shared<Binder>(std::move(binder));
                calc = [shared](int a, unsigned b) {
                    return (*shared)(a, b);
                };
                calc_unary = [shared](int a) {
                    return (*shared)(a);
                };
            }
        }

        template <typename Binder>
        void def_impl(const char* name, Binder binder, dynabridge::type_identity<int(int, unsigned)>) {
            ++def_count;

            if (std::strcmp(name, "add") == 0) {
                add = binder;
            } else if (std::strcmp(name, "calc") == 0) {
                calc = binder;
            } else if (std::strcmp(name, "explicit_add") == 0) {
                explicit_add = binder;
            } else if (std::strcmp(name, "lambda_add") == 0) {
                lambda_add = binder;
            }
        }

        template <typename Binder>
        void def_impl(const char* name, Binder binder, dynabridge::type_identity<void(int)>) {
            ++def_count;

            if (std::strcmp(name, "store") == 0) {
                store = binder;
            }
        }

        template <typename Binder>
        void def_impl(
            const char* name,
            Binder binder,
            dynabridge::type_identity<int(dynabridge::exports::counter, int)>)
        {
            ++def_count;

            if (std::strcmp(name, "add") == 0) {
                counter_add = binder;
            }
        }

        template <typename Binder>
        void def_impl(
            const char* name,
            Binder binder,
            dynabridge::type_identity<int(dynabridge::exports::counter)>)
        {
            ++def_count;

            if (std::strcmp(name, "value") == 0) {
                counter_value = binder;
            }
        }

        template <typename Binder, typename Signature>
        void def_impl(const char*, Binder, dynabridge::type_identity<Signature>) {
            ++def_count;
        }
    };

    struct recorded_call {
        int argc = 0;
        int first = 0;
        int second = 0;
        int overload = 0;

        void operator()(int a) {
            argc = 1;
            first = a;
            second = 0;
            overload = 1;
        }

        unsigned operator()(unsigned a) {
            argc = 1;
            first = static_cast<int>(a);
            second = 0;
            overload = 2;
            return a;
        }

        void operator()(int a, int b) {
            argc = 2;
            first = a;
            second = b;
            overload = 3;
        }

        int operator()(int a, unsigned b) {
            argc = 2;
            first = a;
            second = static_cast<int>(b);
            overload = 4;
            return a + static_cast<int>(b);
        }

        int operator()(unsigned receiver, int value) {
            argc = 2;
            first = static_cast<int>(receiver);
            second = value;
            overload = 5;
            return static_cast<int>(receiver) + value;
        }

        int operator()(counter_handle receiver, int value) {
            argc = 2;
            first = static_cast<int>(receiver.value);
            second = value;
            overload = 6;
            return static_cast<int>(receiver.value) + value;
        }

        int operator()(counter_handle receiver) {
            argc = 1;
            first = static_cast<int>(receiver.value);
            second = 0;
            overload = 7;
            return static_cast<int>(receiver.value);
        }
    };

    struct not_convertible {
    };

    struct wrong_arity {
        int operator()(int) {
            return 0;
        }
    };

    struct wrong_return {
        not_convertible operator()(int, unsigned) {
            return {};
        }
    };

    struct counter_add_export {
        template <typename Counter>
        auto operator()(Counter& counter, int value) const
            -> decltype(counter.add(value))
        {
            return counter.add(value);
        }
    };

    struct counter_value_export {
        template <typename Counter>
        auto operator()(Counter& counter) const
            -> decltype(counter.value())
        {
            return counter.value();
        }
    };

    struct wrong_member_arity {
        int operator()(native_counter&) {
            return 0;
        }
    };

    struct wrong_member_return {
        not_convertible operator()(native_counter&, int) {
            return {};
        }
    };

    struct consume_counter_export {
        int operator()(native_counter& counter, int value) const {
            return counter.add(value);
        }
    };

    struct use_callback_export {
        template <typename Callback>
        int operator()(Callback& callback, int value) const {
            return dynabridge::call_callback(callback, value);
        }
    };

    struct member_object_export {
        int operator()(
            dynabridge::exports::counter& receiver,
            native_counter& other,
            int value) const {
            return receiver.native().add(other.add(value));
        }
    };

    struct descriptor_return_export {
        dynabridge::object_param<
            dynabridge::export_classes::counter,
            dynabridge::export_t> operator()(int) const {
            return {};
        }
    };
}

using export_context_t = dynabridge::fake_backend::export_context_t<recorded_call>;
using export_counter_t = dynabridge::exports::counter;
using import_counter_t = dynabridge::counter<export_context_t>;
using import_addable_t = dynabridge::interfaces::counter_addable<
    import_counter_t, dynabridge::import_t>;
using export_addable_t = dynabridge::interfaces::counter_addable<
    export_counter_t, dynabridge::export_t>;

static_assert(std::is_empty<import_addable_t>::value,
    "import interfaces must not own receiver or context state");
static_assert(std::is_empty<export_addable_t>::value,
    "export interfaces must not own native or backend state");
static_assert(std::is_base_of<import_addable_t, import_counter_t>::value,
    "import classes must publicly compose declared interfaces");
static_assert(std::is_base_of<export_addable_t, export_counter_t>::value,
    "export proxies must publicly compose declared interfaces");
static_assert(!dynabridge::are_interface_types_unique<dynabridge::type_list<
        dynabridge::interface_descriptors::counter_addable<dynabridge::import_t>,
        dynabridge::interface_descriptors::counter_addable<dynabridge::import_t>>>::value,
    "duplicate IMPLEMENTS declarations must be rejected");
static_assert(!dynabridge::are_interface_method_names_unique<
        dynabridge::type_list<
            dynabridge::interface_descriptors::counter_addable<dynabridge::import_t>,
            dynabridge::interface_descriptors::counter_addable<dynabridge::export_t>>,
        dynabridge::type_list<>>::value,
    "composed interfaces must reject duplicate member names");
using export_object_arg_t = dynabridge::object_param<
    dynabridge::export_classes::counter, dynabridge::export_t>;
using import_callable_arg_t = dynabridge::callable_param<
    dynabridge::import_symbols::callback, dynabridge::import_t>;

static_assert(
    dynabridge::is_export_callable_bindable<
        int(export_object_arg_t, int), export_context_t, consume_counter_export>::value,
    "export object descriptors should resolve to native references");

static_assert(
    dynabridge::is_export_callable_bindable<
        int(import_callable_arg_t, int), export_context_t, use_callback_export>::value,
    "export callable descriptors should resolve to imported contexts");

static_assert(
    dynabridge::is_export_member_callable_bindable<
        export_counter_t,
        int(export_object_arg_t, int),
        export_context_t,
        member_object_export>::value,
    "export member probes should understand object descriptor arguments");

static_assert(
    !dynabridge::is_export_callable_bindable<
        export_object_arg_t(int), export_context_t, descriptor_return_export>::value,
    "object and callable return descriptors are intentionally unsupported");

static_assert(
    std::is_same<
        dynabridge::backend_dynamic_value_t<dynabridge::fake_backend>,
        int>::value,
    "backend dynamic value should come from dynamic_value_t");

static_assert(
    dynabridge::are_bridge_params_valid<int, const int&, int&&>::value,
    "bridge params should accept values, const lvalue references, and rvalue references");

static_assert(
    !dynabridge::are_bridge_params_valid<int&>::value,
    "bridge params should reject non-const lvalue references");

using valid_ref_contract = dynabridge::free_callable<void(const int&, int&&)>;

static_assert(
    dynabridge::is_callable_v<valid_ref_contract>,
    "bridge callable signatures should allow const lvalue references and rvalue references");

static_assert(
    dynabridge::is_export_callable_bindable<
        int(int, unsigned), export_context_t, decltype(&add_function)>::value,
    "function pointers with a matching signature should be export bindable");

static_assert(
    dynabridge::is_export_callable_bindable<
        void(int), export_context_t, decltype(&store_function)>::value,
    "void function pointers with a matching signature should be export bindable");

static_assert(
    !dynabridge::is_export_callable_bindable<
        void(int&), export_context_t, decltype(&ref_store_function)>::value,
    "export bindable probe should reject non-const lvalue bridge references");

static_assert(
    !dynabridge::is_export_callable_bindable<
        int(int, unsigned), export_context_t, wrong_arity>::value,
    "export bindable probe should reject callables with the wrong arity");

static_assert(
    !dynabridge::is_export_callable_bindable<
        int(int, unsigned), export_context_t, wrong_return>::value,
    "export bindable probe should reject return values converter<R>::to cannot accept");

static_assert(
    dynabridge::is_export_invocable<
        export_context_t,
        decltype(&add_function),
        int,
        dynabridge::type_list<int&&, unsigned&&>,
        dynabridge::type_list<int, unsigned>>::value,
    "export invocation probe should accept dynamic args with matching converters");

static_assert(
    dynabridge::are_export_arguments_convertible<
        export_context_t,
        dynabridge::type_list<int, unsigned>>::value,
    "export argument probe should accept args convertible from backend dynamic value");

static_assert(
    !dynabridge::are_export_arguments_convertible<
        export_context_t,
        dynabridge::type_list<not_convertible>>::value,
    "export argument probe should reject args without converter<T>::from(dynamic_value_t)");

static_assert(
    dynabridge::is_void_export_invocable<
        export_context_t,
        decltype(&store_function),
        dynabridge::type_list<int&&>,
        dynabridge::type_list<int>>::value,
    "void export invocation probe should accept dynamic args with matching converters");

static_assert(
    dynabridge::is_export_member_callable_bindable<
        export_counter_t,
        int(int),
        export_context_t,
        counter_add_export>::value,
    "export member bindable probe should accept receiver-aware callables");

static_assert(
    dynabridge::is_export_member_callable_bindable<
        export_counter_t,
        int(),
        export_context_t,
        counter_value_export>::value,
    "export member bindable probe should accept no-arg member callables");

static_assert(
    dynabridge::is_export_member_receiver_convertible<
        export_context_t,
        export_counter_t,
        dynabridge::type_list<int>>::value,
    "export member receiver probe should accept bindable self and convertible args");

static_assert(
    !dynabridge::is_export_member_receiver_convertible<
        export_context_t,
        export_counter_t,
        dynabridge::type_list<not_convertible>>::value,
    "export member receiver probe should reject args without converter<T>::from(dynamic_value_t)");

static_assert(
    dynabridge::is_declared_free_callable<
        void(unsigned),
        typename dynabridge::export_constructor_group_for<
            export_counter_t>::type>::value,
    "export constructor group should include declared constructor signatures");

static_assert(
    !dynabridge::is_declared_free_callable<
        void(int),
        typename dynabridge::export_constructor_group_for<
            export_counter_t>::type>::value,
    "export constructor group should reject undeclared constructor signatures");

static_assert(
    dynabridge::is_export_class_constructible<
        export_counter_t,
        export_context_t,
        dynabridge::type_list<unsigned>>::value,
    "export constructor probe should accept generated proxy constructor matches");

static_assert(
    !dynabridge::is_export_class_constructible<
        export_counter_t,
        export_context_t,
        dynabridge::type_list<not_convertible>>::value,
    "export constructor probe should reject generated proxy constructor mismatches");

static_assert(
    dynabridge::is_export_member_callable_bindable<
        export_counter_t,
        int(int),
        export_context_t,
        decltype(&native_counter::add)>::value,
    "export member bindable probe should accept member function pointers");

static_assert(
    dynabridge::is_export_member_callable_bindable<
        export_counter_t,
        int(),
        export_context_t,
        decltype(&native_counter::value)>::value,
    "export member bindable probe should accept const member function pointers");

static_assert(
    !dynabridge::is_export_member_callable_bindable<
        export_counter_t,
        int(int),
        export_context_t,
        wrong_member_arity>::value,
    "export member bindable probe should reject callables with the wrong arity");

static_assert(
    !dynabridge::is_export_member_callable_bindable<
        export_counter_t,
        int(int),
        export_context_t,
        wrong_member_return>::value,
    "export member bindable probe should reject return values converter<R>::to cannot accept");

static_assert(
    dynabridge::is_export_member_invocable<
        export_context_t,
        export_counter_t,
        counter_add_export,
        int,
        unsigned&&,
        dynabridge::type_list<int&&>,
        dynabridge::type_list<int>>::value,
    "export member invocation probe should accept dynamic receiver and args with matching converters");

static_assert(
    dynabridge::is_export_member_invocable<
        export_context_t,
        export_counter_t,
        decltype(&native_counter::add),
        int,
        unsigned&&,
        dynabridge::type_list<int&&>,
        dynabridge::type_list<int>>::value,
    "export member invocation probe should accept member function pointers");

static_assert(
    !dynabridge::is_export_member_invocable<
        export_context_t,
        export_counter_t,
        counter_add_export,
        int,
        not_convertible&&,
        dynabridge::type_list<int&&>,
        dynabridge::type_list<int>>::value,
    "export member invocation probe should reject self handles the backend cannot bind");

static_assert(
    dynabridge::is_forward_invocable<
        export_context_t, dynabridge::no_receiver_t, int, unsigned>::value,
    "forward argument probe should accept arguments with converter<T>::to");

static_assert(
    !dynabridge::is_forward_invocable<
        export_context_t, dynabridge::no_receiver_t, not_convertible>::value,
    "forward argument probe should reject arguments without converter<T>::to");

static_assert(
    dynabridge::is_forward_result_convertible<
        export_context_t, int>::value,
    "forward result probe should accept return values with converter<R>::from");

static_assert(
    !dynabridge::is_forward_result_convertible<
        export_context_t, not_convertible>::value,
    "forward result probe should reject return values without converter<R>::from");

static_assert(
    dynabridge::is_import_object_convertible<
        dynabridge::fake_backend,
        dynabridge::import_symbols::counter,
        export_context_t,
        dynabridge::fake_backend::object_t<
            dynabridge::counter<export_context_t>, dynabridge::import_t>&>::value,
    "imported receivers should use the backend object channel without a converter");

static_assert(
    dynabridge::is_forward_constructible<
        export_context_t, dynabridge::counter<export_context_t>, unsigned>::value,
    "forward construct probe should accept declared constructor args with converters");

static_assert(
    !dynabridge::is_forward_constructible<
        export_context_t, dynabridge::counter<export_context_t>, not_convertible>::value,
    "forward construct probe should reject constructor args without converters");

int main() {
    export_context_t ctx(recorded_call{});
    auto foo_binder = dynabridge::foo(ctx);
    auto bar_binder = dynabridge::bar(ctx);
    auto calc_binder = dynabridge::calc(ctx);

    dynabridge::call_foo(ctx, 1, 2);
    if (ctx.callable_.argc != 2 || ctx.callable_.first != 1
            || ctx.callable_.second != 2 || ctx.callable_.overload != 3) {
        return 1;
    }

    dynabridge::call_bar(ctx, 7);
    if (ctx.callable_.argc != 1 || ctx.callable_.first != 7
            || ctx.callable_.second != 0 || ctx.callable_.overload != 1) {
        return 2;
    }

    bar_binder(8, 9);
    if (ctx.callable_.argc != 2 || ctx.callable_.first != 8
            || ctx.callable_.second != 9 || ctx.callable_.overload != 3) {
        return 21;
    }

    dynabridge::call_foo(ctx, 8, 9);
    if (ctx.callable_.argc != 2 || ctx.callable_.first != 8
            || ctx.callable_.second != 9 || ctx.callable_.overload != 3) {
        return 3;
    }

    dynabridge::call_foo(ctx, 10);
    if (ctx.callable_.argc != 1 || ctx.callable_.first != 10
            || ctx.callable_.second != 0 || ctx.callable_.overload != 1) {
        return 4;
    }

    dynabridge::call_foo(ctx, 11u);
    if (ctx.callable_.argc != 1 || ctx.callable_.first != 11
            || ctx.callable_.second != 0 || ctx.callable_.overload != 2) {
        return 5;
    }

    foo_binder(12);
    if (ctx.callable_.argc != 1 || ctx.callable_.first != 12
            || ctx.callable_.second != 0 || ctx.callable_.overload != 1) {
        return 6;
    }

    foo_binder(13, 14);
    if (ctx.callable_.argc != 2 || ctx.callable_.first != 13
            || ctx.callable_.second != 14 || ctx.callable_.overload != 3) {
        return 7;
    }

    foo_binder(15u);
    if (ctx.callable_.argc != 1 || ctx.callable_.first != 15
            || ctx.callable_.second != 0 || ctx.callable_.overload != 2) {
        return 8;
    }

    ctx.reset_conversions();
    const int result = calc_binder(3, 4u);
    if (result != 7 || ctx.callable_.argc != 2
            || ctx.callable_.first != 3 || ctx.callable_.second != 4
            || ctx.callable_.overload != 4
            || ctx.to_count != 2 || ctx.from_count != 1) {
        return 9;
    }

    auto counter_obj = dynabridge::bind<dynabridge::counter>(ctx, 13u);
    ctx.reset_conversions();
    const int member_result = counter_obj.add(29);
    if (member_result != 42 || ctx.callable_.argc != 2
            || ctx.callable_.first != 13 || ctx.callable_.second != 29
            || ctx.callable_.overload != 5
            || ctx.to_count != 1 || ctx.from_count != 1) {
        return 10;
    }

    ctx.reset_conversions();
    const int member_value = counter_obj.value();
    if (member_value != 13 || ctx.callable_.argc != 1
            || ctx.callable_.first != 13 || ctx.callable_.second != 0
            || ctx.callable_.overload != 2
            || ctx.to_count != 0 || ctx.from_count != 1) {
        return 11;
    }

    ctx.reset_conversions();
    auto constructed_counter = dynabridge::construct_counter(ctx, 21u);
    if (constructed_counter.object().get() != 21
            || ctx.callable_.argc != 1
            || ctx.callable_.first != 21
            || ctx.callable_.overload != 2
            || ctx.to_count != 1
            || ctx.from_count != 0) {
        return 27;
    }

    ctx.reset_conversions();
    auto generic_constructed_counter = dynabridge::construct<dynabridge::counter>(ctx, 34u);
    const int constructed_member_result = generic_constructed_counter.add(8);
    if (constructed_member_result != 42
            || ctx.callable_.argc != 2
            || ctx.callable_.first != 34
            || ctx.callable_.second != 8
            || ctx.callable_.overload != 5
            || ctx.to_count != 2
            || ctx.from_count != 1) {
        return 28;
    }

    int export_first = 0;
    int export_second = 0;
    auto export_calc_binder = dynabridge::create_export_callable_binder<int(int, unsigned)>(
        ctx,
        [&](int a, unsigned b) {
            export_first = a;
            export_second = static_cast<int>(b);
            return a + static_cast<int>(b);
        });

    ctx.reset_conversions();
    const int export_result = export_calc_binder(5, 6u);
    if (export_result != 11 || export_first != 5 || export_second != 6
            || ctx.to_count != 1 || ctx.from_count != 2) {
        return 12;
    }

    int export_void_value = 0;
    auto export_void_binder = dynabridge::create_export_callable_binder<void(int)>(
        ctx,
        [&](int value) {
            export_void_value = value;
        });

    ctx.reset_conversions();
    export_void_binder(42);
    if (export_void_value != 42 || ctx.to_count != 0 || ctx.from_count != 1) {
        return 13;
    }

    fake_module module;
    dynabridge::export_free_callable(ctx, module, "add", add_function);
    dynabridge::export_calc(ctx, module, add_function);
    dynabridge::export_free_callable(ctx, module, "store", store_function);
    dynabridge::export_free_callable<int(int, unsigned)>(
        ctx,
        module,
        "lambda_add",
        [](int a, unsigned b) {
            return a * static_cast<int>(b);
        });
    if (!module.add || !module.calc || !module.store || !module.lambda_add
            || module.def_count != 4) {
        return 14;
    }

    ctx.reset_conversions();
    const int exported_add = module.add(12, 13u);
    if (exported_add != 25 || ctx.to_count != 1 || ctx.from_count != 2) {
        return 15;
    }

    ctx.reset_conversions();
    const int exported_calc = module.calc(12, 13u);
    if (exported_calc != 25 || ctx.to_count != 1 || ctx.from_count != 2) {
        return 16;
    }

    stored_value = 0;
    ctx.reset_conversions();
    module.store(77);
    if (stored_value != 77 || ctx.to_count != 0 || ctx.from_count != 1) {
        return 17;
    }

    ctx.reset_conversions();
    const int exported_lambda = module.lambda_add(6, 7u);
    if (exported_lambda != 42 || ctx.to_count != 1 || ctx.from_count != 2) {
        return 18;
    }

    dynabridge::export_calc<int(int, unsigned)>(
        ctx,
        module,
        [](int a, unsigned b) {
            return a * static_cast<int>(b);
        });
    if (module.def_count != 5) {
        return 19;
    }

    ctx.reset_conversions();
    const int exported_calc_lambda = module.calc(6, 7u);
    if (exported_calc_lambda != 42 || ctx.to_count != 1 || ctx.from_count != 2) {
        return 20;
    }

    dynabridge::export_free_callable<int(int, unsigned)>(
        ctx, module, "explicit_add", add_function);
    dynabridge::export_calc<int(int, unsigned)>(ctx, module, add_function);
    if (!module.explicit_add || module.def_count != 7) {
        return 29;
    }

    ctx.reset_conversions();
    const int exported_explicit_add = module.explicit_add(12, 13u);
    if (exported_explicit_add != 25 || ctx.to_count != 1 || ctx.from_count != 2) {
        return 30;
    }

    ctx.reset_conversions();
    const int exported_explicit_calc = module.calc(12, 13u);
    if (exported_explicit_calc != 25 || ctx.to_count != 1 || ctx.from_count != 2) {
        return 31;
    }

    dynabridge::export_calc(ctx, module)
        .bind<int(int)>(scale_by_ten_function)
        .bind<int(int, unsigned)>(multiply_function{})
        .commit();
    if (!module.calc || !module.calc_unary || module.def_count != 8) {
        return 32;
    }

    ctx.reset_conversions();
    const int exported_calc_function_unary = module.calc_unary(6);
    if (exported_calc_function_unary != 60 || ctx.to_count != 1 || ctx.from_count != 1) {
        return 33;
    }

    ctx.reset_conversions();
    const int exported_calc_functor_binary = module.calc(6, 7u);
    if (exported_calc_functor_binary != 42 || ctx.to_count != 1 || ctx.from_count != 2) {
        return 34;
    }

    auto scale = std::unique_ptr<int>(new int(11));
    dynabridge::export_calc(ctx, module)
        .bind<int(int)>([scale = std::move(scale)](int a) {
            return a * *scale;
        })
        .bind<int(int, unsigned)>([](int a, unsigned b) {
            return a * static_cast<int>(b) + 1;
        })
        .commit();
    if (!module.calc || !module.calc_unary || module.def_count != 9) {
        return 40;
    }

    ctx.reset_conversions();
    const int exported_calc_lambda_unary = module.calc_unary(6);
    if (exported_calc_lambda_unary != 66 || ctx.to_count != 1 || ctx.from_count != 1) {
        return 41;
    }

    ctx.reset_conversions();
    const int exported_calc_lambda_binary = module.calc(6, 7u);
    if (exported_calc_lambda_binary != 43 || ctx.to_count != 1 || ctx.from_count != 2) {
        return 42;
    }

    using select_overloads_t = dynabridge::type_list<
        dynabridge::free_callable<int(unsigned)>,
        dynabridge::free_callable<int(int)>,
        dynabridge::free_callable<
            dynabridge::unmatched_callable_t(dynabridge::unmatched_callable_t)>>;
    auto select_binder = dynabridge::create_export_overload_binder<select_overloads_t>(
        ctx,
        select_overload_function{});

    ctx.reset_conversions();
    const int exported_select_unsigned = select_binder(5);
    if (exported_select_unsigned != 1005 || ctx.to_count != 1 || ctx.from_count != 1) {
        return 35;
    }

    ctx.reset_conversions();
    const int exported_select_int = select_binder(-3);
    if (exported_select_int != -30 || ctx.to_count != 1 || ctx.from_count != 2) {
        return 36;
    }

    export_counter_t::register_all(ctx, module);
    if (!module.counter_add || !module.counter_value || module.def_count != 12) {
        return 21;
    }

    if (module.class_count != 1
            || module.last_class_name == nullptr
            || module.last_member_class_name == nullptr
            || std::strcmp(module.last_class_name, "counter") != 0
            || std::strcmp(module.last_member_class_name, "counter") != 0) {
        return 26;
    }

    ctx.reset_conversions();
    const int exported_counter_add = module.counter_add(13u, 29);
    if (exported_counter_add != 42 || ctx.to_count != 1 || ctx.from_count != 1) {
        return 22;
    }

    ctx.reset_conversions();
    const int exported_counter_value = module.counter_value(13u);
    if (exported_counter_value != 13 || ctx.to_count != 1 || ctx.from_count != 0) {
        return 23;
    }

    auto native_counter_add = dynabridge::create_export_member_callable_binder<export_counter_t, int(int)>(
        ctx,
        &native_counter::add);

    ctx.reset_conversions();
    const int native_counter_add_result = native_counter_add(17u, 25);
    if (native_counter_add_result != 42 || ctx.to_count != 1 || ctx.from_count != 1) {
        return 24;
    }

    auto native_counter_value = dynabridge::create_export_member_callable_binder<export_counter_t, int()>(
        ctx,
        &native_counter::value);

    ctx.reset_conversions();
    const int native_counter_value_result = native_counter_value(42u);
    if (native_counter_value_result != 42 || ctx.to_count != 1 || ctx.from_count != 0) {
        return 25;
    }

    dynabridge::export_instance<export_counter_t>(ctx, module, "global_counter", native_counter{21u});
    if (module.exported_counter == 0 || !module.exported_counter_lifetime || module.def_count != 13) {
        return 37;
    }

    ctx.reset_conversions();
    const int exported_instance_add = module.counter_add(module.exported_counter, 21);
    if (exported_instance_add != 42 || ctx.to_count != 1 || ctx.from_count != 1) {
        return 38;
    }

    native_counter borrowed_counter{31u};
    auto borrowed_object = dynabridge::make_exported<export_counter_t>(
        ctx,
        dynabridge::borrow(borrowed_counter));
    ctx.reset_conversions();
    const int borrowed_instance_value = native_counter_value(borrowed_object.get());
    if (borrowed_instance_value != 31 || ctx.to_count != 1 || ctx.from_count != 0) {
        return 39;
    }

    auto object_runtime = [](unsigned handle, int value) {
        return static_cast<int>(handle) + value;
    };
    dynabridge::fake_backend::context_t<decltype(object_runtime)> object_ctx(
        std::move(object_runtime));
    auto imported_object = dynabridge::bind<dynabridge::counter>(object_ctx, 13u);
    const auto& const_imported_object = imported_object;
    object_ctx.reset_conversions();
    if (dynabridge::call_pass_counter(object_ctx, const_imported_object, 29) != 42
            || object_ctx.to_count != 1 || object_ctx.from_count != 1) {
        return 101;
    }

    auto callable_runtime = [](int callback, int value) {
        return dynabridge::fake_backend::invoke_dynamic_callable(
            static_cast<unsigned>(callback), value);
    };
    dynabridge::fake_backend::context_t<decltype(callable_runtime)> callable_ctx(
        std::move(callable_runtime));
    if (dynabridge::call_pass_transform(callable_ctx, transform_function{}, 4) != 40) {
        return 102;
    }

    auto transform = dynabridge::bind_transform()
        .bind<int(int)>(scale_by_ten_function)
        .bind<int(int, unsigned)>([](int value, unsigned extra) {
            return value * static_cast<int>(extra);
        })
        .build();
    if (dynabridge::call_pass_transform(callable_ctx, std::move(transform), 6) != 60) {
        return 103;
    }

    using consume_counter_sig = int(
        dynabridge::object_param<dynabridge::export_classes::counter, dynabridge::export_t>,
        int);
    auto consume_counter = dynabridge::create_export_callable_binder<consume_counter_sig>(
        ctx,
        [](native_counter& counter, int value) {
            return counter.add(value);
        });
    if (consume_counter(borrowed_object.get(), 11) != 42) {
        return 104;
    }
    try {
        (void)consume_counter(999999, 1);
        return 105;
    } catch (const dynabridge::bad_conversion&) {
    }

    const unsigned dynamic_callback = dynabridge::fake_backend::register_dynamic_callable(
        [](int value) { return value * 3; });
    using use_callback_sig = int(
        dynabridge::callable_param<dynabridge::import_symbols::callback, dynabridge::import_t>,
        int);
    auto use_callback = dynabridge::create_export_callable_binder<use_callback_sig>(
        ctx,
        [](auto& callback, int value) {
            return dynabridge::call_callback(callback, value) + 1;
        });
    if (use_callback(static_cast<int>(dynamic_callback), 7) != 22) {
        return 106;
    }
    try {
        (void)use_callback(-1, 7);
        return 107;
    } catch (const dynabridge::bad_conversion&) {
    }

    dynabridge::fake_backend::context_t<consumer_runtime> constructor_ctx(
        consumer_runtime{});
    auto constructor_counter = dynabridge::bind<dynabridge::counter>(constructor_ctx, 40u);
    auto imported_consumer = dynabridge::construct<dynabridge::consumer>(
        constructor_ctx, constructor_counter, transform_function{});
    if (imported_consumer.object().get() != 50u) {
        return 108;
    }
    if (imported_consumer.combine(constructor_counter, 2) != 92) {
        return 109;
    }
    if (imported_consumer.apply(transform_function{}, 3) != 80) {
        return 110;
    }

    return 0;
}
