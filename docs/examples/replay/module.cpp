#include "api.hpp"
#include <exception>

#define PORTAL_EXPORT_DEF "docs/examples/replay/exports.def"
#include <portal/portal.h>
#include <portal/backends/python.h>

namespace {
PyModuleDef definition = {
    PyModuleDef_HEAD_INIT, "replay_example", nullptr, -1,
    nullptr, nullptr, nullptr, nullptr, nullptr
};
}

PyMODINIT_FUNC PyInit_replay_example() {
    PyObject* object = PyModule_Create(&definition);
    if (!object) return nullptr;
    try {
        static portal::py_backend::export_context_t context;
        portal::py_backend::module_t module(
            object, portal::py_backend::ref_policy::borrowed);
        using Signature = std::string(const std::string&, const std::string&);
        portal::export_run_arena<Signature>(context, module,
            [](const std::string& csv, const std::string& config) {
                return arena::run_arena(csv, config);
            });
        return object;
    } catch (const std::exception& error) {
        Py_DECREF(object);
        PyErr_SetString(PyExc_RuntimeError, error.what());
        return nullptr;
    } catch (...) {
        Py_DECREF(object);
        PyErr_SetString(PyExc_RuntimeError, "replay example initialization failed");
        return nullptr;
    }
}
