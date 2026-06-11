#include "dynabridge/backends/python_api.h"

#include <stdexcept>

#define DYNABRIDGE_IMPORT_DEF "tests/import.def"
#define DYNABRIDGE_EXPORT_DEF "tests/export.def"
#include "dynabridge/bridge.h"
#include "dynabridge/backends/python.h"

namespace dynabridge {
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
    }
}

int dynabridge::native::counter::constructed = 0;
int dynabridge::native::counter::destroyed = 0;

namespace {
    using py_context_t = dynabridge::py_backend::context_t;
    using py_export_context_t = dynabridge::py_backend::export_context_t;
    using owned_counter = dynabridge::native::counter;

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

    dynabridge::py_backend::object_ref object_attr(PyObject* object, const char* name) {
        dynabridge::py_backend::object_ref value(
            PyObject_GetAttrString(object, name),
            dynabridge::py_backend::ref_policy::owned);
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
        dynabridge::py_backend::object_ref args(Py_BuildValue("(i)", a),
            dynabridge::py_backend::ref_policy::owned);
        dynabridge::py_backend::object_ref result(PyObject_CallObject(callable, args.get()),
            dynabridge::py_backend::ref_policy::owned);
        if (!result) {
            throw std::runtime_error("Python call failed");
        }
        return static_cast<int>(PyLong_AsLong(result.get()));
    }

    int call_int_int(PyObject* callable, int a, int b) {
        dynabridge::py_backend::object_ref args(Py_BuildValue("(ii)", a, b),
            dynabridge::py_backend::ref_policy::owned);
        dynabridge::py_backend::object_ref result(PyObject_CallObject(callable, args.get()),
            dynabridge::py_backend::ref_policy::owned);
        if (!result) {
            throw std::runtime_error("Python call failed");
        }
        return static_cast<int>(PyLong_AsLong(result.get()));
    }

    int call_noarg(PyObject* callable) {
        dynabridge::py_backend::object_ref result(PyObject_CallObject(callable, nullptr),
            dynabridge::py_backend::ref_policy::owned);
        if (!result) {
            throw std::runtime_error("Python call failed");
        }
        return static_cast<int>(PyLong_AsLong(result.get()));
    }

    void call_void_int(PyObject* callable, int a) {
        dynabridge::py_backend::object_ref args(Py_BuildValue("(i)", a),
            dynabridge::py_backend::ref_policy::owned);
        dynabridge::py_backend::object_ref result(PyObject_CallObject(callable, args.get()),
            dynabridge::py_backend::ref_policy::owned);
        if (!result) {
            throw std::runtime_error("Python call failed");
        }
    }
}

template <>
struct dynabridge::py_backend::converter<dynabridge::counter<py_context_t>> {
    static PyObject* to(context_t&, dynabridge::counter<py_context_t>& counter) {
        PyObject* object = counter.object().get();
        Py_INCREF(object);
        return object;
    }

    static dynabridge::optional<dynabridge::counter<py_context_t>> from(context_t& ctx, PyObject* object) {
        return dynabridge::optional<dynabridge::counter<py_context_t>>(dynabridge::counter<py_context_t>(
            ctx,
            object_t<dynabridge::counter<py_context_t>>(
                object,
                dynabridge::py_backend::ref_policy::borrowed)));
    }
};

int main() {
    Py_Initialize();

    try {
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

        dynabridge::py_backend::module_t main_module(
            PyImport_AddModule("__main__"),
            dynabridge::py_backend::ref_policy::borrowed);

        auto record_ctx = dynabridge::import_from<dynabridge::import_symbols::foo, py_context_t>(
            main_module);
        dynabridge::call_foo(record_ctx, 1, 2);
        if (object_long_attr(main_module.get(), "last_argc") != 2
                || object_long_attr(main_module.get(), "last_first") != 1
                || object_long_attr(main_module.get(), "last_second") != 2) {
            return 2;
        }

        auto bar_ctx = dynabridge::import_from<dynabridge::import_symbols::bar, py_context_t>(
            main_module);
        dynabridge::bar(bar_ctx)(7);
        if (object_long_attr(main_module.get(), "last_argc") != 1
                || object_long_attr(main_module.get(), "last_first") != 7) {
            return 3;
        }

        auto calc_ctx = dynabridge::import_from<dynabridge::import_symbols::calc, py_context_t>(
            "__main__");
        if (dynabridge::call_calc(calc_ctx, 3, 4u) != 7) {
            return 4;
        }

        auto counter_ctx = dynabridge::import_from<dynabridge::import_symbols::counter, py_context_t>(
            main_module);
        auto receiver = object_attr(main_module.get(), "receiver");
        auto counter = dynabridge::bind_receiver<dynabridge::counter>(
            counter_ctx,
            receiver.get(),
            dynabridge::py_backend::ref_policy::borrowed);
        if (counter.add(29) != 42 || counter.value() != 13) {
            return 5;
        }

        auto constructed_counter = dynabridge::construct<dynabridge::counter>(counter_ctx, 21u);
        if (constructed_counter.value() != 21 || constructed_counter.add(21) != 42) {
            return 12;
        }

        py_export_context_t export_ctx;
        dynabridge::py_backend::module_t module(
            PyModule_New("dynabridge_py_backend_test"),
            dynabridge::py_backend::ref_policy::owned);

        dynabridge::py_backend::object_ref bad_int(PyUnicode_FromString("not an int"),
            dynabridge::py_backend::ref_policy::owned);
        auto maybe_int = dynabridge::from_optional<int>(export_ctx, bad_int.get());
        if (maybe_int || PyErr_Occurred()) {
            return 34;
        }

        dynabridge::py_backend::object_ref negative_int(PyLong_FromLong(-1),
            dynabridge::py_backend::ref_policy::owned);
        auto maybe_unsigned = dynabridge::from_optional<unsigned>(export_ctx, negative_int.get());
        if (maybe_unsigned || PyErr_Occurred()) {
            return 35;
        }

        bool caught_bad_conversion = false;
        try {
            (void)dynabridge::from_cast<int>(export_ctx, bad_int.get());
        } catch (const dynabridge::bad_conversion&) {
            caught_bad_conversion = true;
        }
        if (!caught_bad_conversion || PyErr_Occurred()) {
            return 30;
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
        dynabridge::py_backend::object_ref calc_function_functor(
            PyObject_GetAttrString(module.get(), "calc"),
            dynabridge::py_backend::ref_policy::owned);
        if (call_int(calc_function_functor.get(), 6) != 60
                || call_int_int(calc_function_functor.get(), 6, 7) != 42) {
            return 40;
        }

        dynabridge::export_calc(export_ctx, module)
            .bind<int(int)>([](int a) {
                return a * 11;
            })
            .bind<int(int, unsigned)>([](int a, unsigned b) {
                return a * static_cast<int>(b) + 1;
            })
            .commit();

        dynabridge::py_backend::object_ref add(PyObject_GetAttrString(module.get(), "add"),
            dynabridge::py_backend::ref_policy::owned);
        dynabridge::py_backend::object_ref calc(PyObject_GetAttrString(module.get(), "calc"),
            dynabridge::py_backend::ref_policy::owned);
        dynabridge::py_backend::object_ref store(PyObject_GetAttrString(module.get(), "store"),
            dynabridge::py_backend::ref_policy::owned);

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

        dynabridge::py_backend::object_ref bad_add_args(Py_BuildValue("(Oi)", bad_int.get(), 1),
            dynabridge::py_backend::ref_policy::owned);
        dynabridge::py_backend::object_ref bad_add_result(
            PyObject_CallObject(add.get(), bad_add_args.get()),
            dynabridge::py_backend::ref_policy::owned);
        if (bad_add_result || !PyErr_ExceptionMatches(PyExc_TypeError)) {
            return 31;
        }
        PyErr_Clear();

        stored_value = 0;
        call_void_int(store.get(), 77);
        if (stored_value != 77) {
            return 15;
        }

        dynabridge::exports::counter<dynabridge::native::counter>::register_all(export_ctx, module);

        dynabridge::py_backend::object_ref counter_class(PyObject_GetAttrString(module.get(), "counter"),
            dynabridge::py_backend::ref_policy::owned);
        dynabridge::py_backend::object_ref constructor_args(Py_BuildValue("(I)", 13u),
            dynabridge::py_backend::ref_policy::owned);
        dynabridge::py_backend::object_ref instance(PyObject_CallObject(counter_class.get(), constructor_args.get()),
            dynabridge::py_backend::ref_policy::owned);
        if (!instance || owned_counter::constructed != 1 || owned_counter::destroyed != 0) {
            return 16;
        }

        dynabridge::py_backend::object_ref member_add(PyObject_GetAttrString(instance.get(), "add"),
            dynabridge::py_backend::ref_policy::owned);
        dynabridge::py_backend::object_ref member_value(PyObject_GetAttrString(instance.get(), "value"),
            dynabridge::py_backend::ref_policy::owned);

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
        auto borrowed_object = dynabridge::make_exported<
            dynabridge::exports::counter<dynabridge::native::counter>>(
            export_ctx,
            dynabridge::borrow(borrowed_counter));
        dynabridge::py_backend::object_ref borrowed_value(
            PyObject_GetAttrString(borrowed_object.get(), "value"),
            dynabridge::py_backend::ref_policy::owned);
        dynabridge::py_backend::object_ref borrowed_add(
            PyObject_GetAttrString(borrowed_object.get(), "add"),
            dynabridge::py_backend::ref_policy::owned);
        if (call_noarg(borrowed_value.get()) != 31 || call_int(borrowed_add.get(), 11) != 42) {
            return 32;
        }

        dynabridge::export_instance<
            dynabridge::exports::counter<dynabridge::native::counter>>(
            export_ctx,
            module,
            "globalCounter",
            dynabridge::borrow(borrowed_counter));
        dynabridge::py_backend::object_ref global_counter(
            PyObject_GetAttrString(module.get(), "globalCounter"),
            dynabridge::py_backend::ref_policy::owned);
        dynabridge::py_backend::object_ref global_add(
            PyObject_GetAttrString(global_counter.get(), "add"),
            dynabridge::py_backend::ref_policy::owned);
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
