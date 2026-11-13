#include "portal/backends/python.h"
#include <cstdio>
#include <cstring>

namespace {
using backend = portal::py_backend;
int destroyed = 0;
backend::object_ref* observed_owner = nullptr;
PyObject* expected_replacement = nullptr;
bool saw_replacement = false;

void destroy_probe(PyObject* self) {
    ++destroyed;
    if (observed_owner != nullptr) {
        saw_replacement = observed_owner->get() == expected_replacement;
    }
    Py_TYPE(self)->tp_free(self);
}

PyObject* make_probe() {
    static PyTypeObject type = {PyVarObject_HEAD_INIT(nullptr, 0)};
    if ((type.tp_flags & Py_TPFLAGS_READY) == 0) {
        type.tp_name = "portal.ResetOwnershipProbe";
        type.tp_basicsize = sizeof(PyObject);
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_dealloc = destroy_probe;
        if (PyType_Ready(&type) != 0) return nullptr;
    }
    return PyObject_New(PyObject, &type);
}
}

int main(int argc, char** argv) {
    Py_Initialize();
    const bool observe = argc > 1 && std::strcmp(argv[1], "observe") == 0;
    bool passed = false;
    {
        backend::object_ref owner(make_probe(), backend::ref_policy::owned);
        if (!owner) return 3;
        if (observe) {
            observed_owner = &owner;
            expected_replacement = Py_None;
            owner.reset(Py_None);
            observed_owner = nullptr;
            passed = saw_replacement && destroyed == 1;
            std::printf("old-object destructor saw replacement: %s; destroyed: %d\n",
                        saw_replacement ? "yes" : "no", destroyed);
        } else {
            owner.reset(owner.get());
            passed = destroyed == 0 && Py_REFCNT(owner.get()) == 1;
            std::printf("destroyed during same-pointer borrowed reset: %d\n", destroyed);
        }
    }
    Py_Finalize();
    return passed ? 0 : 2;
}
