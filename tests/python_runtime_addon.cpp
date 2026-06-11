#define DYNABRIDGE_IMPORT_DEF "tests/import.def"
#define DYNABRIDGE_EXPORT_DEF "tests/export.def"
#include "dynabridge/bridge.h"
#include "dynabridge/backends/python.h"

#include <exception>
#include <stdexcept>

using py_context_t = dynabridge::py_backend::context_t;
using py_export_context_t = dynabridge::py_backend::export_context_t;

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

    PyObject* raise_error(const char* message) {
        PyErr_SetString(PyExc_RuntimeError, message);
        return nullptr;
    }

    PyObject* raise_error(const std::exception& error) {
        return raise_error(error.what());
    }

    PyObject* call_imported_calc(PyObject*, PyObject* args) {
        try {
            PyObject* callable = nullptr;
            int a = 0;
            unsigned b = 0;
            if (PyArg_ParseTuple(args, "OiI", &callable, &a, &b) == 0) {
                return nullptr;
            }

            py_context_t ctx(callable, dynabridge::py_backend::ref_policy::borrowed);
            return dynabridge::py_backend::converter<int>::to(
                ctx,
                dynabridge::call_calc(ctx, a, b));
        } catch (const std::exception& error) {
            return raise_error(error);
        }
    }

    PyObject* call_imported_foo(PyObject*, PyObject* args) {
        try {
            PyObject* callable = nullptr;
            int a = 0;
            int b = 0;
            if (PyArg_ParseTuple(args, "Oii", &callable, &a, &b) == 0) {
                return nullptr;
            }

            py_context_t ctx(callable, dynabridge::py_backend::ref_policy::borrowed);
            dynabridge::call_foo(ctx, a, b);
            Py_RETURN_NONE;
        } catch (const std::exception& error) {
            return raise_error(error);
        }
    }

    PyObject* call_imported_counter_add(PyObject*, PyObject* args) {
        try {
            PyObject* callable = nullptr;
            PyObject* receiver = nullptr;
            int value = 0;
            if (PyArg_ParseTuple(args, "OOi", &callable, &receiver, &value) == 0) {
                return nullptr;
            }

            py_context_t ctx(callable, dynabridge::py_backend::ref_policy::borrowed);
            auto counter = dynabridge::bind_receiver<dynabridge::counter>(
                ctx,
                receiver,
                dynabridge::py_backend::ref_policy::borrowed);
            return dynabridge::py_backend::converter<int>::to(ctx, counter.add(value));
        } catch (const std::exception& error) {
            return raise_error(error);
        }
    }

    PyObject* call_imported_counter_value(PyObject*, PyObject* args) {
        try {
            PyObject* callable = nullptr;
            PyObject* receiver = nullptr;
            if (PyArg_ParseTuple(args, "OO", &callable, &receiver) == 0) {
                return nullptr;
            }

            py_context_t ctx(callable, dynabridge::py_backend::ref_policy::borrowed);
            auto counter = dynabridge::bind_receiver<dynabridge::counter>(
                ctx,
                receiver,
                dynabridge::py_backend::ref_policy::borrowed);
            return dynabridge::py_backend::converter<int>::to(ctx, counter.value());
        } catch (const std::exception& error) {
            return raise_error(error);
        }
    }

    PyObject* construct_imported_counter_add(PyObject*, PyObject* args) {
        try {
            PyObject* callable = nullptr;
            unsigned handle = 0;
            int value = 0;
            if (PyArg_ParseTuple(args, "OIi", &callable, &handle, &value) == 0) {
                return nullptr;
            }

            py_context_t ctx(callable, dynabridge::py_backend::ref_policy::borrowed);
            auto counter = dynabridge::construct<dynabridge::counter>(ctx, handle);
            return dynabridge::py_backend::converter<int>::to(ctx, counter.add(value));
        } catch (const std::exception& error) {
            return raise_error(error);
        }
    }

    PyMethodDef module_methods[] = {
        {
            "callImportedCalc",
            call_imported_calc,
            METH_VARARGS,
            nullptr
        },
        {
            "callImportedFoo",
            call_imported_foo,
            METH_VARARGS,
            nullptr
        },
        {
            "callImportedCounterAdd",
            call_imported_counter_add,
            METH_VARARGS,
            nullptr
        },
        {
            "callImportedCounterValue",
            call_imported_counter_value,
            METH_VARARGS,
            nullptr
        },
        {
            "constructImportedCounterAdd",
            construct_imported_counter_add,
            METH_VARARGS,
            nullptr
        },
        {nullptr, nullptr, 0, nullptr}
    };

    PyModuleDef module_def = {
        PyModuleDef_HEAD_INIT,
        "dynabridge_python_runtime_addon",
        nullptr,
        -1,
        module_methods
    };
}

PyMODINIT_FUNC PyInit_dynabridge_python_runtime_addon() {
    PyObject* module_object = PyModule_Create(&module_def);
    if (module_object == nullptr) {
        return nullptr;
    }

    try {
        static py_export_context_t ctx;
        dynabridge::py_backend::module_t module(
            module_object,
            dynabridge::py_backend::ref_policy::borrowed);

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
    } catch (const std::exception& error) {
        Py_DECREF(module_object);
        PyErr_SetString(PyExc_RuntimeError, error.what());
        return nullptr;
    } catch (...) {
        Py_DECREF(module_object);
        PyErr_SetString(PyExc_RuntimeError, "dynabridge Python runtime addon init failed");
        return nullptr;
    }

    return module_object;
}
