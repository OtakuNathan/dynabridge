#include "portal/backends/python_api.h"

#include <stdexcept>
#include <string>

#define PORTAL_IMPORT_DEF "tests/import.def"
#define PORTAL_EXPORT_DEF "tests/export.def"
#include "portal/bridge.h"
#include "portal/backends/python.h"

struct conversion_probe {
    PyObject* value;
    int* calls;
    int fail_at;
};

namespace portal {
    template <>
    struct py_backend::converter<conversion_probe> {
        static PyObject* to(context_t&, conversion_probe probe) {
            if (++*probe.calls == probe.fail_at) {
                throw std::runtime_error("injected conversion failure");
            }
            Py_INCREF(probe.value);
            return probe.value;
        }
    };
}

namespace portal {
    namespace native {
        class counter {
        public:
            static int constructed;
            static int destroyed;

            int handle = 0;

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
            static int constructed;
            static int destroyed;

            template <typename Callback>
            consumer(counter& source, Callback& callback)
                : base_(portal::call_callback(callback, source.value())) {
                ++constructed;
            }

            ~consumer() {
                ++destroyed;
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
int portal::native::consumer::constructed = 0;
int portal::native::consumer::destroyed = 0;

namespace {
    using py_context_t = portal::py_backend::context_t;
    using py_export_context_t = portal::py_backend::export_context_t;
    using owned_counter = portal::native::counter;

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

    portal::py_backend::object_ref object_attr(PyObject* object, const char* name) {
        portal::py_backend::object_ref value(
            PyObject_GetAttrString(object, name),
            portal::py_backend::ref_policy::owned);
        if (!value) {
            throw std::runtime_error("missing Python attribute");
        }
        return value;
    }

    long object_long_attr(PyObject* object, const char* name) {
        auto value = object_attr(object, name);
        return PyLong_AsLong(value.get());
    }

    int call_int(PyObject* callable, int a) {
        portal::py_backend::object_ref args(Py_BuildValue("(i)", a),
            portal::py_backend::ref_policy::owned);
        portal::py_backend::object_ref result(PyObject_CallObject(callable, args.get()),
            portal::py_backend::ref_policy::owned);
        if (!result) {
            throw std::runtime_error("Python call failed");
        }
        return static_cast<int>(PyLong_AsLong(result.get()));
    }

    int call_int_int(PyObject* callable, int a, int b) {
        portal::py_backend::object_ref args(Py_BuildValue("(ii)", a, b),
            portal::py_backend::ref_policy::owned);
        portal::py_backend::object_ref result(PyObject_CallObject(callable, args.get()),
            portal::py_backend::ref_policy::owned);
        if (!result) {
            throw std::runtime_error("Python call failed");
        }
        return static_cast<int>(PyLong_AsLong(result.get()));
    }

    std::string call_string(PyObject* callable, const std::string& a) {
        portal::py_backend::object_ref arg(
            PyUnicode_FromStringAndSize(a.data(), static_cast<Py_ssize_t>(a.size())),
            portal::py_backend::ref_policy::owned);
        portal::py_backend::object_ref result(
            PyObject_CallFunctionObjArgs(callable, arg.get(), nullptr),
            portal::py_backend::ref_policy::owned);
        if (!result) {
            throw std::runtime_error("Python call failed");
        }
        Py_ssize_t size = 0;
        const char* data = PyUnicode_AsUTF8AndSize(result.get(), &size);
        if (data == nullptr) {
            PyErr_Clear();
            throw std::runtime_error("Python string result conversion failed");
        }
        return std::string(data, static_cast<std::size_t>(size));
    }

    int call_noarg(PyObject* callable) {
        portal::py_backend::object_ref result(PyObject_CallObject(callable, nullptr),
            portal::py_backend::ref_policy::owned);
        if (!result) {
            throw std::runtime_error("Python call failed");
        }
        return static_cast<int>(PyLong_AsLong(result.get()));
    }

    void call_void_int(PyObject* callable, int a) {
        portal::py_backend::object_ref args(Py_BuildValue("(i)", a),
            portal::py_backend::ref_policy::owned);
        portal::py_backend::object_ref result(PyObject_CallObject(callable, args.get()),
            portal::py_backend::ref_policy::owned);
        if (!result) {
            throw std::runtime_error("Python call failed");
        }
    }
}

int main() {
    Py_Initialize();

    try {
        {
            using backend = portal::py_backend;
            backend::object_ref tracked(PyList_New(0), backend::ref_policy::owned);
            backend::context_t ctx;
            // Both the first and a later conversion failure must leave no
            // owned argument behind, independent of argument evaluation order.
            for (int fail_at : {1, 2}) {
                int calls = 0;
                conversion_probe probe{tracked.get(), &calls, fail_at};
                bool threw = false;
                try {
                    backend::invoke_contract<portal::free_callable<void(
                        conversion_probe, conversion_probe)>>(
                            ctx, portal::no_receiver_t{}, probe, probe);
                } catch (const std::runtime_error&) {
                    threw = true;
                }
                if (!threw || calls != fail_at || Py_REFCNT(tracked.get()) != 1) {
                    return 70;
                }
            }
            // All arguments convert, but dispatch fails (no callable).
            int calls = 0;
            conversion_probe probe{tracked.get(), &calls, 0};
            try {
                backend::invoke_contract<portal::free_callable<void(
                    conversion_probe, conversion_probe)>>(
                        ctx, portal::no_receiver_t{}, probe, probe);
                return 71;
            } catch (const std::runtime_error&) {
                PyErr_Clear();
            }
            if (calls != 2 || Py_REFCNT(tracked.get()) != 1) {
                return 72;
            }
        }
        const char* script =
            "last_argc = 0\n"
            "last_first = 0\n"
            "last_second = 0\n"
            "def record(*args):\n"
            "    global last_argc, last_first, last_second\n"
            "    last_argc = len(args)\n"
            "    last_first = int(args[0]) if len(args) > 0 else 0\n"
            "    last_second = int(args[1]) if len(args) > 1 else 0\n"
            "foo = record\n"
            "bar = record\n"
            "def calc(a, b):\n"
            "    return int(a) + int(b)\n"
            "def echo(text):\n"
            "    return '[' + text + ']'\n"
            "def callback(value):\n"
            "    return int(value) * 3\n"
            "def pass_counter(obj, value):\n"
            "    return int(obj.handle) + int(value)\n"
            "def pass_transform(fn, value):\n"
            "    return int(fn(value))\n"
            "class Receiver:\n"
            "    pass\n"
            "def make_receiver(handle):\n"
            "    result = Receiver()\n"
            "    result.handle = int(handle)\n"
            "    return result\n"
            "receiver = Receiver()\n"
            "receiver.handle = 13\n"
            "def counter_call(receiver_or_handle, *args):\n"
            "    if not hasattr(receiver_or_handle, 'handle'):\n"
            "        return make_receiver(receiver_or_handle)\n"
            "    receiver = receiver_or_handle\n"
            "    if len(args) == 0:\n"
            "        return int(receiver.handle)\n"
            "    return int(receiver.handle) + int(args[0])\n"
            "counter = counter_call\n";

        if (PyRun_SimpleString(script) != 0) {
            return 1;
        }

        portal::py_backend::module_t main_module(
            PyImport_AddModule("__main__"),
            portal::py_backend::ref_policy::borrowed);

        auto record_ctx = portal::import_from<portal::import_symbols::foo, py_context_t>(
            main_module);
        portal::call_foo(record_ctx, 1, 2);
        if (object_long_attr(main_module.get(), "last_argc") != 2
                || object_long_attr(main_module.get(), "last_first") != 1
                || object_long_attr(main_module.get(), "last_second") != 2) {
            return 2;
        }

        auto bar_ctx = portal::import_from<portal::import_symbols::bar, py_context_t>(
            main_module);
        portal::bar(bar_ctx)(7);
        if (object_long_attr(main_module.get(), "last_argc") != 1
                || object_long_attr(main_module.get(), "last_first") != 7) {
            return 3;
        }

        auto calc_ctx = portal::import_from<portal::import_symbols::calc, py_context_t>(
            "__main__");
        if (portal::call_calc(calc_ctx, 3, 4u) != 7) {
            return 4;
        }

        auto echo_ctx = portal::import_from<portal::import_symbols::echo, py_context_t>(
            main_module);
        const std::string utf8_text = "h\xC3\xA9llo \xE4\xB8\xAD";
        const std::string embedded_text(std::string("a\0b", 3) + utf8_text);
        if (portal::call_echo(echo_ctx, utf8_text) != "[" + utf8_text + "]"
                || portal::call_echo(echo_ctx, embedded_text) != "[" + embedded_text + "]") {
            return 60;
        }

        auto counter_ctx = portal::import_from<portal::import_symbols::counter, py_context_t>(
            main_module);
        auto receiver = object_attr(main_module.get(), "receiver");
        auto counter = portal::bind_receiver<portal::counter>(
            counter_ctx,
            receiver.get(),
            portal::py_backend::ref_policy::borrowed);
        if (counter.add(29) != 42 || counter.value() != 13) {
            return 5;
        }

        auto constructed_counter = portal::construct<portal::counter>(counter_ctx, 21u);
        if (constructed_counter.value() != 21 || constructed_counter.add(21) != 42) {
            return 12;
        }

        auto pass_counter_ctx = portal::import_from<
            portal::import_symbols::pass_counter, py_context_t>(main_module);
        if (portal::call_pass_counter(pass_counter_ctx, counter, 29) != 42) {
            return 41;
        }

        auto pass_transform_ctx = portal::import_from<
            portal::import_symbols::pass_transform, py_context_t>(main_module);
        if (portal::call_pass_transform(
                pass_transform_ctx, transform_function{}, 4) != 40) {
            return 42;
        }
        auto built_transform = portal::bind_transform()
            .bind<int(int)>(scale_by_ten_function)
            .bind<int(int, unsigned)>([](int value, unsigned extra) {
                return value * static_cast<int>(extra);
            })
            .build();
        if (portal::call_pass_transform(
                pass_transform_ctx, std::move(built_transform), 6) != 60) {
            return 43;
        }

        py_export_context_t export_ctx;
        portal::py_backend::module_t module(
            PyModule_New("portal_py_backend_test"),
            portal::py_backend::ref_policy::owned);

        portal::py_backend::object_ref bad_int(PyUnicode_FromString("not an int"),
            portal::py_backend::ref_policy::owned);
        auto maybe_int = portal::from_optional<int>(export_ctx, bad_int.get());
        if (maybe_int || PyErr_Occurred()) {
            return 34;
        }

        portal::py_backend::object_ref negative_int(PyLong_FromLong(-1),
            portal::py_backend::ref_policy::owned);
        auto maybe_unsigned = portal::from_optional<unsigned>(export_ctx, negative_int.get());
        if (maybe_unsigned || PyErr_Occurred()) {
            return 35;
        }

        bool caught_bad_conversion = false;
        try {
            (void)portal::from_cast<int>(export_ctx, bad_int.get());
        } catch (const portal::bad_conversion&) {
            caught_bad_conversion = true;
        }
        if (!caught_bad_conversion || PyErr_Occurred()) {
            return 30;
        }

        portal::py_backend::object_ref not_a_string(PyLong_FromLong(7),
            portal::py_backend::ref_policy::owned);
        auto maybe_string = portal::from_optional<std::string>(
            export_ctx, not_a_string.get());
        if (maybe_string || PyErr_Occurred()) {
            return 62;
        }
        // bad_int holds a str, so the string channel accepts its bytes.
        auto maybe_bad_int_text = portal::from_optional<std::string>(
            export_ctx, bad_int.get());
        if (!maybe_bad_int_text || maybe_bad_int_text.value() != std::string("not an int")
                || PyErr_Occurred()) {
            return 63;
        }
        bool caught_string_conversion = false;
        try {
            (void)portal::from_cast<std::string>(export_ctx, not_a_string.get());
        } catch (const portal::bad_conversion&) {
            caught_string_conversion = true;
        }
        if (!caught_string_conversion || PyErr_Occurred()) {
            return 64;
        }

        portal::py_backend::object_ref lone_surrogate(
            PyUnicode_FromOrdinal(0xD800),
            portal::py_backend::ref_policy::owned);
        auto maybe_surrogate = portal::from_optional<std::string>(
            export_ctx, lone_surrogate.get());
        if (maybe_surrogate || PyErr_Occurred()) {
            return 69;
        }

        bool caught_invalid_utf8 = false;
        try {
            (void)portal::py_backend::converter<std::string>::to(
                export_ctx, std::string("\xFF", 1));
        } catch (const std::runtime_error&) {
            caught_invalid_utf8 = true;
        }
        if (!caught_invalid_utf8 || !PyErr_ExceptionMatches(PyExc_UnicodeDecodeError)) {
            return 70;
        }
        PyErr_Clear();

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
        portal::py_backend::object_ref calc_function_functor(
            PyObject_GetAttrString(module.get(), "calc"),
            portal::py_backend::ref_policy::owned);
        if (call_int(calc_function_functor.get(), 6) != 60
                || call_int_int(calc_function_functor.get(), 6, 7) != 42) {
            return 40;
        }

        portal::export_calc(export_ctx, module)
            .bind<int(int)>([](int a) {
                return a * 11;
            })
            .bind<int(int, unsigned)>([](int a, unsigned b) {
                return a * static_cast<int>(b) + 1;
            })
            .commit();

        portal::py_backend::object_ref add(PyObject_GetAttrString(module.get(), "add"),
            portal::py_backend::ref_policy::owned);
        portal::py_backend::object_ref calc(PyObject_GetAttrString(module.get(), "calc"),
            portal::py_backend::ref_policy::owned);
        portal::py_backend::object_ref store(PyObject_GetAttrString(module.get(), "store"),
            portal::py_backend::ref_policy::owned);

#if PY_VERSION_HEX >= 0x03080000
        if (PyVectorcall_Function(add.get()) == nullptr
                || PyVectorcall_Function(calc.get()) == nullptr
                || PyVectorcall_Function(store.get()) == nullptr) {
            return 13;
        }
#endif

        if (call_int_int(add.get(), 12, 13) != 25
                || call_int(calc.get(), 6) != 66
                || call_int_int(calc.get(), 6, 7) != 43) {
            return 14;
        }

        portal::export_echo(export_ctx, module)
            .bind<std::string(std::string)>([](std::string text) {
                return "<" + text + ">";
            })
            .bind<int(int)>([](int value) {
                return value * 2;
            })
            .commit();
        portal::py_backend::object_ref echo_function(
            PyObject_GetAttrString(module.get(), "echo"),
            portal::py_backend::ref_policy::owned);
        if (call_string(echo_function.get(), utf8_text) != "<" + utf8_text + ">") {
            return 65;
        }
        if (call_string(echo_function.get(), embedded_text) != "<" + embedded_text + ">") {
            return 67;
        }
        if (call_int(echo_function.get(), 21) != 42) {
            return 68;
        }
        portal::py_backend::object_ref none_args(Py_BuildValue("(O)", Py_None),
            portal::py_backend::ref_policy::owned);
        portal::py_backend::object_ref none_result(
            PyObject_CallObject(echo_function.get(), none_args.get()),
            portal::py_backend::ref_policy::owned);
        if (none_result || !PyErr_ExceptionMatches(PyExc_TypeError)) {
            return 66;
        }
        PyErr_Clear();

        portal::py_backend::object_ref bad_add_args(Py_BuildValue("(Oi)", bad_int.get(), 1),
            portal::py_backend::ref_policy::owned);
        portal::py_backend::object_ref bad_add_result(
            PyObject_CallObject(add.get(), bad_add_args.get()),
            portal::py_backend::ref_policy::owned);
        if (bad_add_result || !PyErr_ExceptionMatches(PyExc_TypeError)) {
            return 31;
        }
        PyErr_Clear();

        stored_value = 0;
        call_void_int(store.get(), 77);
        if (stored_value != 77) {
            return 15;
        }

        portal::exports::counter::register_all(export_ctx, module);

        portal::py_backend::object_ref counter_class(PyObject_GetAttrString(module.get(), "counter"),
            portal::py_backend::ref_policy::owned);
        portal::py_backend::object_ref missing_constructor_args(
            PyTuple_New(0), portal::py_backend::ref_policy::owned);
        portal::py_backend::object_ref missing_constructor_result(
            PyObject_CallObject(counter_class.get(), missing_constructor_args.get()),
            portal::py_backend::ref_policy::owned);
        if (missing_constructor_result || !PyErr_ExceptionMatches(PyExc_TypeError)
                || owned_counter::constructed != 0 || owned_counter::destroyed != 0) {
            return 52;
        }
        PyErr_Clear();

        portal::py_backend::object_ref constructor_args(Py_BuildValue("(I)", 13u),
            portal::py_backend::ref_policy::owned);
        portal::py_backend::object_ref instance(PyObject_CallObject(counter_class.get(), constructor_args.get()),
            portal::py_backend::ref_policy::owned);
        if (!instance || owned_counter::constructed != 1 || owned_counter::destroyed != 0) {
            return 16;
        }

        portal::py_backend::object_ref member_add(PyObject_GetAttrString(instance.get(), "add"),
            portal::py_backend::ref_policy::owned);
        portal::py_backend::object_ref member_value(PyObject_GetAttrString(instance.get(), "value"),
            portal::py_backend::ref_policy::owned);

#if PY_VERSION_HEX >= 0x03080000
        if (PyVectorcall_Function(member_add.get()) == nullptr
                || PyVectorcall_Function(member_value.get()) == nullptr) {
            return 17;
        }
#endif

        if (call_int(member_add.get(), 29) != 42 || call_noarg(member_value.get()) != 13) {
            return 18;
        }

        member_add.reset();
        member_value.reset();
        instance.reset();
        if (owned_counter::destroyed != 1) {
            return 19;
        }

        owned_counter borrowed_counter(31u);
        auto borrowed_object = portal::make_exported<
            portal::exports::counter>(
            export_ctx,
            portal::borrow(borrowed_counter));
        portal::py_backend::object_ref borrowed_value(
            PyObject_GetAttrString(borrowed_object.get(), "value"),
            portal::py_backend::ref_policy::owned);
        portal::py_backend::object_ref borrowed_add(
            PyObject_GetAttrString(borrowed_object.get(), "add"),
            portal::py_backend::ref_policy::owned);
        if (call_noarg(borrowed_value.get()) != 31 || call_int(borrowed_add.get(), 11) != 42) {
            return 32;
        }

        using consume_counter_sig = int(
            portal::object_param<portal::export_classes::counter, portal::export_t>,
            int);
        portal::export_consume_counter<consume_counter_sig>(
            export_ctx,
            module,
            [](owned_counter& counter, int value) {
                return counter.add(value);
            });
        portal::py_backend::object_ref consume_counter(
            PyObject_GetAttrString(module.get(), "consume_counter"),
            portal::py_backend::ref_policy::owned);
        portal::py_backend::object_ref eleven(PyLong_FromLong(11),
            portal::py_backend::ref_policy::owned);
        portal::py_backend::object_ref consumed(PyObject_CallFunctionObjArgs(
            consume_counter.get(), borrowed_object.get(), eleven.get(), nullptr),
            portal::py_backend::ref_policy::owned);
        if (!consumed || PyLong_AsLong(consumed.get()) != 42) {
            return 44;
        }

        using use_callback_sig = int(
            portal::callable_param<portal::import_symbols::callback, portal::import_t>,
            int);
        portal::export_use_callback<use_callback_sig>(
            export_ctx,
            module,
            [](auto& callback, int value) {
                return portal::call_callback(callback, value) + 1;
            });
        portal::py_backend::object_ref use_callback(
            PyObject_GetAttrString(module.get(), "use_callback"),
            portal::py_backend::ref_policy::owned);
        auto callback = object_attr(main_module.get(), "callback");
        portal::py_backend::object_ref seven(PyLong_FromLong(7),
            portal::py_backend::ref_policy::owned);
        portal::py_backend::object_ref callback_result(PyObject_CallFunctionObjArgs(
            use_callback.get(), callback.get(), seven.get(), nullptr),
            portal::py_backend::ref_policy::owned);
        if (!callback_result || PyLong_AsLong(callback_result.get()) != 22) {
            return 45;
        }

        portal::py_backend::object_ref wrong_object_result(PyObject_CallFunctionObjArgs(
            consume_counter.get(), seven.get(), seven.get(), nullptr),
            portal::py_backend::ref_policy::owned);
        if (wrong_object_result || !PyErr_ExceptionMatches(PyExc_TypeError)) {
            return 46;
        }
        PyErr_Clear();

        portal::exports::consumer::register_all(export_ctx, module);
        portal::py_backend::object_ref consumer_class(
            PyObject_GetAttrString(module.get(), "consumer"),
            portal::py_backend::ref_policy::owned);
        portal::py_backend::object_ref consumer_instance(PyObject_CallFunctionObjArgs(
            consumer_class.get(), borrowed_object.get(), callback.get(), nullptr),
            portal::py_backend::ref_policy::owned);
        if (!consumer_instance || portal::native::consumer::constructed != 1) {
            return 48;
        }

        portal::py_backend::object_ref combine(
            PyObject_GetAttrString(consumer_instance.get(), "combine"),
            portal::py_backend::ref_policy::owned);
        portal::py_backend::object_ref combine_result(PyObject_CallFunctionObjArgs(
            combine.get(), borrowed_object.get(), eleven.get(), nullptr),
            portal::py_backend::ref_policy::owned);
        if (!combine_result || PyLong_AsLong(combine_result.get()) != 135) {
            return 49;
        }

        portal::py_backend::object_ref apply(
            PyObject_GetAttrString(consumer_instance.get(), "apply"),
            portal::py_backend::ref_policy::owned);
        portal::py_backend::object_ref apply_result(PyObject_CallFunctionObjArgs(
            apply.get(), callback.get(), seven.get(), nullptr),
            portal::py_backend::ref_policy::owned);
        if (!apply_result || PyLong_AsLong(apply_result.get()) != 114) {
            return 50;
        }

        combine.reset();
        apply.reset();
        consumer_instance.reset();
        if (portal::native::consumer::destroyed != 1) {
            return 51;
        }

        portal::py_backend::object_ref wrong_callable_result(PyObject_CallFunctionObjArgs(
            use_callback.get(), seven.get(), seven.get(), nullptr),
            portal::py_backend::ref_policy::owned);
        if (wrong_callable_result || !PyErr_ExceptionMatches(PyExc_TypeError)) {
            return 47;
        }
        PyErr_Clear();

        portal::export_instance<
            portal::exports::counter>(
            export_ctx,
            module,
            "globalCounter",
            portal::borrow(borrowed_counter));
        portal::py_backend::object_ref global_counter(
            PyObject_GetAttrString(module.get(), "globalCounter"),
            portal::py_backend::ref_policy::owned);
        portal::py_backend::object_ref global_add(
            PyObject_GetAttrString(global_counter.get(), "add"),
            portal::py_backend::ref_policy::owned);
        if (call_int(global_add.get(), 11) != 42) {
            return 33;
        }
    } catch (...) {
        PyErr_Print();
        Py_Finalize();
        return 99;
    }

    Py_Finalize();
    return 0;
}
