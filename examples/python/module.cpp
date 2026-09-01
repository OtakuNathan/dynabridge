#include "examples/common/native.h"

#define DYNABRIDGE_EXPORT_DEF "examples/common/export.def"
#include "dynabridge/bridge.h"
#include "dynabridge/backends/python.h"

#include <exception>

namespace {
    PyModuleDef module_definition = {
        PyModuleDef_HEAD_INIT,
        "dynabridge_python_example",
        nullptr,
        -1,
        nullptr
    };
}

PyMODINIT_FUNC PyInit_dynabridge_python_example() {
    PyObject* module_object = PyModule_Create(&module_definition);
    if (module_object == nullptr) {
        return nullptr;
    }

    try {
        static dynabridge::py_backend::export_context_t ctx;
        dynabridge::py_backend::module_t module(
            module_object,
            dynabridge::py_backend::ref_policy::borrowed);

        dynabridge::export_add(ctx, module, dynabridge::example_native::add);
        dynabridge::exports::counter::register_all(ctx, module);
        return module_object;
    } catch (const std::exception& error) {
        Py_DECREF(module_object);
        PyErr_SetString(PyExc_RuntimeError, error.what());
        return nullptr;
    } catch (...) {
        Py_DECREF(module_object);
        PyErr_SetString(PyExc_RuntimeError, "dynabridge example initialization failed");
        return nullptr;
    }
}
