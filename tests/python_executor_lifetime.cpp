#include "portal/extensions/flux_foundry/python_interpreter_executor.h"
#include <atomic>
#include <cstdio>

struct owned_task {
    PyObject* object;
    std::atomic<int>* invoked;
    std::atomic<int>* unsafe;
    owned_task(PyObject* obj, std::atomic<int>& ran, std::atomic<int>& bad)
        : object(obj), invoked(&ran), unsafe(&bad) { Py_INCREF(object); }
    owned_task(owned_task&& other) noexcept
        : object(other.object), invoked(other.invoked), unsafe(other.unsafe) {
        other.object = nullptr;
    }
    owned_task(const owned_task&) = delete;
    void operator()() noexcept {
        if (!PyGILState_Check()) ++*unsafe;
        ++*invoked;
    }
    ~owned_task() noexcept {
        if (!object) return;
        if (!PyGILState_Check()) {
            ++*unsafe; // Report regression without invoking unsafe Python code.
            return;
        }
        Py_DECREF(object);
    }
};

int main() {
    Py_Initialize();
    std::atomic<int> invoked{0}, unsafe{0};
    PyObject* object = PyList_New(0);
    const auto references = Py_REFCNT(object);
    {
        portal::flux_foundry_extensions::python_interpreter_executor executor;
        for (int i = 0; i < 1000; ++i)
            executor.dispatch(flux_foundry::task_wrapper_sbo(owned_task(object, invoked, unsafe)));
        executor.shutdown();
        executor.shutdown();
    }
    const bool ok = invoked == 1000 && unsafe == 0 && Py_REFCNT(object) == references;
    std::printf("invoked=%d unsafe=%d remaining_refs=%zd\n",
        invoked.load(), unsafe.load(), Py_REFCNT(object));
    Py_DECREF(object);
    Py_Finalize();
    return ok ? 0 : 1;
}
