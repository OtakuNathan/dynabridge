#define PORTAL_IMPORT_DEF "tests/import.def"
#define PORTAL_EXPORT_DEF "tests/export.def"
#include "portal/portal.h"
#include "portal/backends/python.h"

#include <cstdio>
#include <exception>
#include <stdexcept>
#include <type_traits>

using backend = portal::py_backend;
using ref = backend::object_ref;
constexpr auto owned = backend::ref_policy::owned;

namespace portal { namespace native {
class counter {
public:
    explicit counter(unsigned value) : value_(value) {}
    int value() const { return static_cast<int>(value_); }
    int add(int n) const { return value() + n; }
private:
    unsigned value_;
};
class consumer {
public:
    template <typename Callback>
    consumer(counter& source, Callback& callback) {
        portal::call_callback(callback, source.value());
    }
    int combine(counter& source, int n) const { return source.add(n); }
    template <typename Callback>
    int apply(Callback& callback, int n) const {
        return portal::call_callback(callback, n);
    }
};
}}

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

PyObject* fail_with_original(PyObject* original, PyObject*) {
    PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(original)), original);
    return nullptr;
}

void overwrite_error_on_release(PyObject* capsule) {
    auto* finalized = static_cast<int*>(PyCapsule_GetPointer(capsule, nullptr));
    ++*finalized;
    PyErr_SetString(PyExc_KeyError, "argument finalizer changed pending error");
}

void check_argument_cleanup(PyObject* original) {
    static_assert(std::is_nothrow_copy_constructible<backend::python_error>::value, "copy");
    static_assert(std::is_nothrow_move_constructible<backend::python_error>::value, "move");
    static_assert(std::is_nothrow_destructible<backend::python_error>::value, "destroy");
    static PyMethodDef method = {"fail", fail_with_original, METH_VARARGS, nullptr};
    backend::context_t ctx(PyCFunction_New(&method, original), owned);
    int finalized = 0;
    for (bool use_owner : {false, true}) {
        PyObject* arg = PyCapsule_New(&finalized, nullptr, overwrite_error_on_release);
        require(arg != nullptr, "cannot create argument finalizer probe");
        bool caught = false;
        try {
            if (use_owner) {
                backend::invoke_impl<void>(ctx, portal::no_receiver_t{}, ref(arg, owned));
            } else {
                backend::invoke_impl<void>(ctx, portal::no_receiver_t{}, arg);
            }
        } catch (const backend::python_error& error) {
            caught = true;
            error.restore();
            PyObject *type = nullptr, *value = nullptr, *traceback = nullptr;
            PyErr_Fetch(&type, &value, &traceback);
            const bool same_value = value == original;
            Py_XDECREF(type);
            Py_XDECREF(value);
            Py_XDECREF(traceback);
            require(same_value, "argument finalizer replaced original exception");
        }
        require(caught, "argument cleanup probe did not throw");
    }
    require(finalized == 2, "argument references were not released exactly once");
    require(!PyErr_Occurred(), "argument finalizer left a pending error");
}

void run_script(PyObject* globals, const char* script) {
    ref result(PyRun_String(script, Py_file_input, globals, globals), owned);
    if (!result) {
        throw backend::python_error("exception regression script failed");
    }
}

void run() {
    ref globals(PyDict_New(), owned);
    require(PyDict_SetItemString(globals.get(), "__builtins__", PyEval_GetBuiltins()) == 0,
        "cannot initialize globals");
    run_script(globals.get(), R"py(
import gc
import sys
class CustomError(ValueError):
    def __str__(self):
        raise AssertionError('bridge must not stringify the original exception')
original = CustomError('original value', 123)
original.detail = {'identity': object()}
cause = LookupError('cause')
def raising(*args):
    raise original from cause
def success(*args):
    return 42
)py");
    backend::context_t raising(PyDict_GetItemString(globals.get(), "raising"));
    backend::context_t success(PyDict_GetItemString(globals.get(), "success"));
    PyObject* original = PyDict_GetItemString(globals.get(), "original");
    check_argument_cleanup(original);

    // C++ catches own the Python error, leaving the interpreter usable. A copy
    // and exception_ptr retain the same value/traceback, never a formatted copy.
    std::exception_ptr saved;
    bool caught = false;
    try {
        portal::call_callback(raising, 1);
    } catch (const backend::python_error& error) {
        caught = true;
        require(!PyErr_Occurred(), "error remained pending during C++ handling");
        require(portal::call_callback(success, 1) == 42, "recovery call failed");
        backend::python_error copied(error);
        backend::python_error moved(std::move(copied));
        moved.restore();
        PyObject *type = nullptr, *value = nullptr, *traceback = nullptr;
        PyErr_Fetch(&type, &value, &traceback);
        require(value == original && traceback != nullptr, "copy lost exception identity");
        Py_XDECREF(type);
        Py_XDECREF(value);
        Py_XDECREF(traceback);
        saved = std::current_exception();
    }
    require(caught, "Python exception was not captured");
    try {
        std::rethrow_exception(saved);
    } catch (const backend::python_error& error) {
        PyErr_SetString(PyExc_KeyError, "unrelated intervening error");
        error.restore();
        require(PyErr_ExceptionMatches(PyExc_ValueError), "rethrow lost original type");
        PyErr_Clear();
    }
    saved = nullptr; // Release before interpreter shutdown, with the GIL held.
    {
        backend::python_error fallback("missing Python error");
        fallback.restore();
        require(PyErr_ExceptionMatches(PyExc_RuntimeError), "missing-error fallback failed");
        PyErr_Clear();
    }

    backend::export_context_t ctx;
    backend::module_t module(PyModule_New("exception_test"), owned);
    portal::exports::counter::register_all(ctx, module);
    portal::exports::consumer::register_all(ctx, module);
    using callback_sig = int(portal::callable_param<
        portal::import_symbols::callback, portal::import_t>, int);
    portal::export_use_callback<callback_sig>(ctx, module, [](auto& cb, int n) {
        return portal::call_callback(cb, n);
    });
    portal::export_calc(ctx, module)
        .bind<int(int)>([&](int n) { return portal::call_callback(raising, n); })
        .bind<int(int, unsigned)>([&](int n, unsigned m) {
            return portal::call_calc(raising, n, m);
        }).commit();
    portal::export_free_callable<void(int)>(ctx, module, "voidRaise", [&](int n) {
        portal::call_foo(raising, n);
    });
    portal::export_free_callable<int(int)>(ctx, module, "recover", [&](int n) {
        try {
            return portal::call_callback(raising, n);
        } catch (const backend::python_error&) {
            require(!PyErr_Occurred(), "pending error after catch");
            return portal::call_callback(success, n);
        }
    });
    portal::export_free_callable<int(int)>(ctx, module, "replace", [&](int n) {
        try {
            return portal::call_callback(raising, n);
        } catch (const backend::python_error&) {
            throw std::logic_error("replacement native failure");
        }
    });
    portal::export_free_callable<int(int)>(ctx, module, "unwind", [&](int n) {
        struct cleanup {
            ~cleanup() { PyErr_SetString(PyExc_KeyError, "cleanup changed pending error"); }
        } guard;
        return portal::call_callback(raising, n);
    });
    portal::export_free_callable<int(int)>(ctx, module, "nativeFailure", [](int) -> int {
        throw std::runtime_error("native failure");
    });
    portal::export_free_callable<int(int)>(ctx, module, "unknownFailure", [](int) -> int {
        throw 7;
    });
    require(PyDict_SetItemString(globals.get(), "m", module.get()) == 0, "cannot publish module");
    run_script(globals.get(), R"py(
def check(call):
    original.__traceback__ = None
    try:
        call()
    except CustomError as caught:
        assert caught is original
        assert caught.args == ('original value', 123)
        assert caught.__cause__ is cause
        assert caught.__suppress_context__
        assert caught.detail is original.detail
        frames = []
        tb = caught.__traceback__
        while tb is not None:
            frames.append(tb.tb_frame.f_code.co_name)
            tb = tb.tb_next
        assert frames[-1] == 'raising', frames
    else:
        raise AssertionError('callback exception was swallowed')
    finally:
        original.__traceback__ = None

counter = m.counter(1)
consumer = m.consumer(counter, success)
for call in (
    lambda: m.use_callback(raising, 1),
    lambda: m.calc(1),
    lambda: m.calc(1, 2),
    lambda: m.voidRaise(1),
    lambda: m.consumer(counter, raising),
    lambda: consumer.apply(raising, 1),
    lambda: m.unwind(1),
    lambda: m.use_callback(lambda n: m.use_callback(raising, n), 1),
):
    check(call)

# BaseException subclasses must survive just like ordinary Exception subclasses.
for exc in (KeyboardInterrupt('interrupt'), SystemExit(3), MemoryError('memory')):
    def special(n):
        raise exc
    try:
        m.use_callback(special, 1)
    except BaseException as caught:
        assert caught is exc
    else:
        raise AssertionError('BaseException was swallowed')
    exc.__traceback__ = None

for call, kind, message in (
    (lambda: m.nativeFailure(1), RuntimeError, 'native failure'),
    (lambda: m.unknownFailure(1), RuntimeError, 'portal Python callback failed'),
    (lambda: m.replace(1), RuntimeError, 'replacement native failure'),
    (lambda: m.use_callback(1, 1), TypeError, None),
):
    try:
        call()
    except kind as caught:
        if message is not None:
            assert str(caught) == message
    else:
        raise AssertionError('native error mapping changed')
original.__traceback__ = None
assert m.recover(1) == 42
original.__traceback__ = None
check(lambda: m.use_callback(raising, 1))
gc.collect()
before = (sys.getrefcount(original), sys.getrefcount(raising), sys.getrefcount(cause))
for _ in range(10000):
    check(lambda: m.use_callback(raising, 1))
gc.collect()
assert before == (sys.getrefcount(original), sys.getrefcount(raising), sys.getrefcount(cause))
assert m.use_callback(success, 1) == 42
)py");
    require(!PyErr_Occurred(), "pending Python error after tests");
}
}

int main() {
    Py_Initialize();
    int status = 0;
    try {
        run();
    } catch (const backend::python_error& error) {
        error.restore();
        PyErr_Print();
        status = 1;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        if (PyErr_Occurred()) PyErr_Print();
        status = 1;
    }
    // All C++ Python owners, including active exception objects, are gone.
    if (Py_FinalizeEx() != 0) status = 1;
    return status;
}
