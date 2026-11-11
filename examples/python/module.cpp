#include "examples/common/native.h"

#define PORTAL_EXPORT_DEF "examples/common/export.def"
#include "portal/bridge.h"
#include "portal/backends/python.h"

#include <exception>

namespace {
    PyModuleDef module_definition = {
        PyModuleDef_HEAD_INIT,
        "portal_python_example",
        nullptr,
        -1,
        nullptr
    };
}

PyMODINIT_FUNC PyInit_portal_python_example() {
    PyObject* module_object = PyModule_Create(&module_definition);
    if (module_object == nullptr) {
        return nullptr;
    }

    try {
        static portal::py_backend::export_context_t ctx;
        portal::py_backend::module_t module(
            module_object,
            portal::py_backend::ref_policy::borrowed);

        portal::export_add(ctx, module, portal::example_native::add);
        portal::exports::counter::register_all(ctx, module);
        return module_object;
    } catch (const std::exception& error) {
        Py_DECREF(module_object);
        PyErr_SetString(PyExc_RuntimeError, error.what());
        return nullptr;
    } catch (...) {
        Py_DECREF(module_object);
        PyErr_SetString(PyExc_RuntimeError, "portal example initialization failed");
        return nullptr;
    }
}
