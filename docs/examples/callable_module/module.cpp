#include <exception>
#include <string>

#define PORTAL_EXPORT_DEF "docs/examples/callable_module/exports.def"
#include <portal/bridge.h>
#include <portal/backends/python.h>

namespace arena {
// A string stub for demonstrating callable registration, not a replay engine.
std::string run_arena(const std::string& csv, const std::string& config) {
    return csv + ":" + config;
}
}

namespace {
PyModuleDef definition = {
    PyModuleDef_HEAD_INIT, "callable_example", nullptr, -1,
    nullptr, nullptr, nullptr, nullptr, nullptr
};
}

PyMODINIT_FUNC PyInit_callable_example() {
    PyObject* object = PyModule_Create(&definition);
    if (!object) return nullptr;
    try {
        static portal::py_backend::export_context_t context;
        portal::py_backend::module_t module(
            object, portal::py_backend::ref_policy::borrowed);
        using Signature = std::string(const std::string&, const std::string&);

        portal::export_deduced(context, module, arena::run_arena);
        portal::export_run_arena<Signature>(context, module,
            [](const std::string& csv, const std::string& config) {
                return arena::run_arena(csv, config);
            });
        const std::string prefix = "saved:";
        portal::export_captured<Signature>(context, module,
            [prefix](const std::string& csv, const std::string& config) {
                return prefix + arena::run_arena(csv, config);
            });
        portal::export_pointer_lambda(context, module,
            +[](const std::string& csv, const std::string& config) {
                return arena::run_arena(csv, config);
            });
        portal::export_calc(context, module)
            .bind<int(int)>([](int value) { return value * 10; })
            .bind<int(int, unsigned)>([](int value, unsigned factor) {
                return value * static_cast<int>(factor);
            })
            .commit();
        return object;
    } catch (const std::exception& error) {
        Py_DECREF(object);
        PyErr_SetString(PyExc_RuntimeError, error.what());
        return nullptr;
    } catch (...) {
        Py_DECREF(object);
        PyErr_SetString(PyExc_RuntimeError, "callable example initialization failed");
        return nullptr;
    }
}
