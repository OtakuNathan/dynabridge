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
            dynabridge::type_identity<int(dynabridge::exports::counter<native_counter>, int)>)
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
            dynabridge::type_identity<int(dynabridge::exports::counter<native_counter>)>)
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
}

template <typename Context>
struct dynabridge::fake_backend::converter<dynabridge::counter<Context>> {
    template <typename T>
    static counter_handle to(context_t<T>& ctx, dynabridge::counter<Context>& counter) noexcept {
        ++ctx.to_count;
        return counter_handle{counter.object().get()};
    }

    template <typename T>
    static dynabridge::optional<dynabridge::counter<Context>> from(context_t<T>& ctx, unsigned handle) {
        ++ctx.from_count;
        return dynabridge::optional<dynabridge::counter<Context>>(
            dynabridge::counter<Context>(ctx, handle));
    }
};

using export_context_t = dynabridge::fake_backend::export_context_t<recorded_call>;
using export_counter_t = dynabridge::exports::counter<native_counter>;

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
    dynabridge::is_forward_invocable<
        export_context_t, dynabridge::counter<export_context_t>, int, int>::value,
    "forward member argument probe should accept receiver and arguments with converters");

static_assert(
    !dynabridge::is_forward_invocable<
        export_context_t, not_convertible, int, int>::value,
    "forward member argument probe should reject receivers without converter<Receiver>::to");

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
            || ctx.callable_.overload != 6
            || ctx.to_count != 2 || ctx.from_count != 1) {
        return 10;
    }

    ctx.reset_conversions();
    const int member_value = counter_obj.value();
    if (member_value != 13 || ctx.callable_.argc != 1
            || ctx.callable_.first != 13 || ctx.callable_.second != 0
            || ctx.callable_.overload != 7
            || ctx.to_count != 1 || ctx.from_count != 1) {
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
            || ctx.callable_.overload != 6
            || ctx.to_count != 3
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

    return 0;
}
