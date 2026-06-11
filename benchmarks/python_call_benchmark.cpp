#include "dynabridge/backends/python_api.h"

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#define DYNABRIDGE_IMPORT_DEF "tests/import.def"
#define DYNABRIDGE_EXPORT_DEF "tests/export.def"
#include "dynabridge/bridge.h"
#include "dynabridge/backends/python.h"

#if defined(DYNABRIDGE_HAS_PYBIND11)
#include <pybind11/pybind11.h>
#endif

#if defined(DYNABRIDGE_HAS_NANOBIND)
#include <nanobind/nanobind.h>
#endif

namespace {
    volatile long long sink = 0;

    int add_function(int a, unsigned b) {
        return a + static_cast<int>(b);
    }

    struct select_overload_function {
        int operator()(unsigned value) const {
            return 1000 + static_cast<int>(value);
        }

        int operator()(int value) const {
            return value * 10;
        }
    };

    struct bench_result {
        std::string name;
        long long ns = 0;
        long long checksum = 0;
        long long expected_checksum = 0;
    };

    std::size_t iterations_from_env() {
        const char* value = std::getenv("DYNABRIDGE_BENCH_ITERS");
        if (value == nullptr) {
            return 200000;
        }

        const long parsed = std::strtol(value, nullptr, 10);
        return parsed > 0 ? static_cast<std::size_t>(parsed) : 200000;
    }

    void throw_python_error(const char* message) {
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
        throw std::runtime_error(message);
    }

    int to_int(PyObject* object) {
        if (object == nullptr) {
            throw_python_error("Python call returned null");
        }

        const long value = PyLong_AsLong(object);
        if (PyErr_Occurred()) {
            Py_DECREF(object);
            throw_python_error("Python result conversion failed");
        }

        Py_DECREF(object);
        return static_cast<int>(value);
    }

    int raw_tuple_call(PyObject* callable) {
        PyObject* a = PyLong_FromLong(1);
        PyObject* b = PyLong_FromUnsignedLong(2);
        PyObject* args = PyTuple_New(2);
        if (a == nullptr || b == nullptr || args == nullptr) {
            Py_XDECREF(a);
            Py_XDECREF(b);
            Py_XDECREF(args);
            throw_python_error("Python argument allocation failed");
        }

        PyTuple_SET_ITEM(args, 0, a);
        PyTuple_SET_ITEM(args, 1, b);

        PyObject* result = PyObject_CallObject(callable, args);
        Py_DECREF(args);
        return to_int(result);
    }

    int raw_tuple_call1(PyObject* callable, long value) {
        PyObject* a = PyLong_FromLong(value);
        PyObject* args = PyTuple_New(1);
        if (a == nullptr || args == nullptr) {
            Py_XDECREF(a);
            Py_XDECREF(args);
            throw_python_error("Python argument allocation failed");
        }

        PyTuple_SET_ITEM(args, 0, a);

        PyObject* result = PyObject_CallObject(callable, args);
        Py_DECREF(args);
        return to_int(result);
    }

#if PY_VERSION_HEX >= 0x03080000
    int raw_vectorcall(PyObject* callable) {
        PyObject* args[] = {
            PyLong_FromLong(1),
            PyLong_FromUnsignedLong(2)
        };

        if (args[0] == nullptr || args[1] == nullptr) {
            Py_XDECREF(args[0]);
            Py_XDECREF(args[1]);
            throw_python_error("Python argument allocation failed");
        }

        PyObject* result = PyObject_Vectorcall(callable, args, 2, nullptr);
        Py_DECREF(args[0]);
        Py_DECREF(args[1]);
        return to_int(result);
    }

    int raw_vectorcall1(PyObject* callable, long value) {
        PyObject* arg = PyLong_FromLong(value);
        if (arg == nullptr) {
            throw_python_error("Python argument allocation failed");
        }

        PyObject* args[] = { arg };
        PyObject* result = PyObject_Vectorcall(callable, args, 1, nullptr);
        Py_DECREF(arg);
        return to_int(result);
    }
#endif

#if defined(DYNABRIDGE_HAS_PYBIND11) && PY_VERSION_HEX >= 0x03080000
    int pybind11_manual_vectorcall(pybind11::function& callable) {
        namespace py = pybind11;

        py::object a = py::cast(1);
        py::object b = py::cast(2u);
        PyObject* args[] = {
            a.ptr(),
            b.ptr()
        };

        PyObject* result = PyObject_Vectorcall(callable.ptr(), args, 2, nullptr);
        if (result == nullptr) {
            throw py::error_already_set();
        }

        return py::reinterpret_steal<py::object>(result).cast<int>();
    }
#endif

#if defined(DYNABRIDGE_HAS_NANOBIND) && PY_VERSION_HEX >= 0x03080000
    int nanobind_manual_vectorcall(nanobind::object& callable) {
        namespace nb = nanobind;

        nb::object a = nb::cast(1);
        nb::object b = nb::cast(2u);
        PyObject* args[] = {
            a.ptr(),
            b.ptr()
        };

        PyObject* result = PyObject_Vectorcall(callable.ptr(), args, 2, nullptr);
        if (result == nullptr) {
            throw nb::python_error();
        }

        return nb::cast<int>(nb::steal<nb::object>(result));
    }
#endif

    template <typename Fn>
    bench_result run_benchmark(const char* name, std::size_t iterations, int expected, Fn&& fn) {
        long long checksum = 0;
        for (std::size_t i = 0; i < 1000; ++i) {
            checksum += fn();
        }

        const auto start = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < iterations; ++i) {
            checksum += fn();
        }
        const auto stop = std::chrono::steady_clock::now();

        sink = checksum;
        const long long expected_checksum =
            static_cast<long long>(expected) * static_cast<long long>(iterations + 1000);
        if (checksum != expected_checksum) {
            throw std::runtime_error("benchmark checksum mismatch");
        }

        return bench_result{
            name,
            std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count(),
            checksum,
            expected_checksum
        };
    }

    void print_results(const std::vector<bench_result>& results, std::size_t iterations) {
        std::cout << "iterations: " << iterations << '\n';
#if defined(DYNABRIDGE_HAS_PYBIND11)
        std::cout << "pybind11: enabled\n";
#else
        std::cout << "pybind11: not found at configure time\n";
#endif
#if defined(DYNABRIDGE_HAS_NANOBIND)
        std::cout << "nanobind: enabled\n";
#else
        std::cout << "nanobind: not found at configure time\n";
#endif
        std::cout << '\n';

        std::cout << std::left << std::setw(32) << "case"
                  << std::right << std::setw(14) << "ns/call"
                  << std::setw(14) << "calls/sec"
                  << std::setw(14) << "checksum" << '\n';

        for (const auto& result : results) {
            const double ns_per_call = static_cast<double>(result.ns) / iterations;
            const double calls_per_sec = 1000000000.0 / ns_per_call;
            std::cout << std::left << std::setw(32) << result.name
                      << std::right << std::setw(14) << std::fixed << std::setprecision(1)
                      << ns_per_call
                      << std::setw(14) << std::setprecision(0) << calls_per_sec
                      << std::setw(14) << result.checksum << '\n';
        }
    }
}

int main() {
    Py_Initialize();
#if defined(DYNABRIDGE_HAS_NANOBIND)
    // This benchmark embeds Python directly instead of entering through NB_MODULE.
    nanobind::detail::nb_module_exec(nullptr, nullptr);
#endif

    try {
        if (PyRun_SimpleString("def calc(a, b):\n    return a + b\n") != 0) {
            throw_python_error("failed to define Python benchmark function");
        }

        PyObject* main = PyImport_AddModule("__main__");
        if (main == nullptr) {
            throw_python_error("failed to import __main__");
        }

        PyObject* globals = PyModule_GetDict(main);
        PyObject* calc = PyDict_GetItemString(globals, "calc");
        if (calc == nullptr || !PyCallable_Check(calc)) {
            throw_python_error("missing Python calc callable");
        }

        dynabridge::py_backend::context_t ctx(calc, dynabridge::py_backend::ref_policy::borrowed);
        dynabridge::py_backend::export_context_t export_ctx;
        dynabridge::py_backend::module_t module(
            PyModule_New("dynabridge_python_call_benchmark"),
            dynabridge::py_backend::ref_policy::owned);
        dynabridge::export_free_callable(export_ctx, module, "add", add_function);
        dynabridge::export_calc(export_ctx, module)
            .bind<int(int)>([](int a) {
                return a * 10;
            })
            .bind<int(int, unsigned)>([](int a, unsigned b) {
                return add_function(a, b);
            })
            .commit();

        using select_overloads_t = dynabridge::type_list<
            dynabridge::free_callable<int(unsigned)>,
            dynabridge::free_callable<int(int)>,
            dynabridge::free_callable<
                dynabridge::unmatched_callable_t(dynabridge::unmatched_callable_t)>>;
        dynabridge::export_free_callable_overloads_impl<select_overloads_t>(
            export_ctx,
            module,
            "select",
            select_overload_function{});

        dynabridge::py_backend::object_ref dynabridge_add(
            PyObject_GetAttrString(module.get(), "add"),
            dynabridge::py_backend::ref_policy::owned);
        dynabridge::py_backend::object_ref dynabridge_calc(
            PyObject_GetAttrString(module.get(), "calc"),
            dynabridge::py_backend::ref_policy::owned);
        dynabridge::py_backend::object_ref dynabridge_select(
            PyObject_GetAttrString(module.get(), "select"),
            dynabridge::py_backend::ref_policy::owned);
        if (!dynabridge_add) {
            throw_python_error("missing dynabridge exported add callable");
        }
        if (!dynabridge_calc || !dynabridge_select) {
            throw_python_error("missing dynabridge exported overload callable");
        }

        const std::size_t iterations = iterations_from_env();

        std::vector<bench_result> results;
        results.push_back(run_benchmark("dynabridge import", iterations, 3, [&ctx]() {
            return dynabridge::call_calc(ctx, 1, 2u);
        }));

        results.push_back(run_benchmark("raw C API tuple", iterations, 3, [calc]() {
            return raw_tuple_call(calc);
        }));

#if PY_VERSION_HEX >= 0x03080000
        results.push_back(run_benchmark("raw C API vectorcall", iterations, 3, [calc]() {
            return raw_vectorcall(calc);
        }));
#endif

        results.push_back(run_benchmark("dynabridge export tuple", iterations, 3, [&dynabridge_add]() {
            return raw_tuple_call(dynabridge_add.get());
        }));
        results.push_back(run_benchmark("dynabridge overload tuple 1", iterations, 10, [&dynabridge_calc]() {
            return raw_tuple_call1(dynabridge_calc.get(), 1);
        }));
        results.push_back(run_benchmark("dynabridge overload tuple 2", iterations, 3, [&dynabridge_calc]() {
            return raw_tuple_call(dynabridge_calc.get());
        }));
        results.push_back(run_benchmark("dynabridge overload fallback tuple", iterations, -10, [&dynabridge_select]() {
            return raw_tuple_call1(dynabridge_select.get(), -1);
        }));

#if PY_VERSION_HEX >= 0x03080000
        if (PyVectorcall_Function(dynabridge_add.get()) != nullptr) {
            results.push_back(run_benchmark("dynabridge export vectorcall", iterations, 3, [&dynabridge_add]() {
                return raw_vectorcall(dynabridge_add.get());
            }));
        }
        if (PyVectorcall_Function(dynabridge_calc.get()) != nullptr) {
            results.push_back(run_benchmark("dynabridge overload vectorcall 1", iterations, 10, [&dynabridge_calc]() {
                return raw_vectorcall1(dynabridge_calc.get(), 1);
            }));
            results.push_back(run_benchmark("dynabridge overload vectorcall 2", iterations, 3, [&dynabridge_calc]() {
                return raw_vectorcall(dynabridge_calc.get());
            }));
        }
        if (PyVectorcall_Function(dynabridge_select.get()) != nullptr) {
            results.push_back(run_benchmark("dynabridge overload fallback vc", iterations, -10, [&dynabridge_select]() {
                return raw_vectorcall1(dynabridge_select.get(), -1);
            }));
        }
#endif

#if defined(DYNABRIDGE_HAS_PYBIND11)
        namespace py = pybind11;
        py::function py_calc = py::reinterpret_borrow<py::function>(calc);
        py::cpp_function py_add([](int a, unsigned b) {
            return add_function(a, b);
        });
        py::module_ main_module = py::module_::import("__main__");
        main_module.def("pybind11_calc_overload", [](int a) {
            return a * 10;
        });
        main_module.def("pybind11_calc_overload", [](int a, unsigned b) {
            return add_function(a, b);
        });
        py::function pybind11_calc_overload =
            main_module.attr("pybind11_calc_overload").cast<py::function>();

        results.push_back(run_benchmark("pybind11 function call", iterations, 3, [&py_calc]() {
            return py_calc(1, 2u).cast<int>();
        }));
#if PY_VERSION_HEX >= 0x03080000
        results.push_back(run_benchmark("pybind11 manual vectorcall", iterations, 3, [&py_calc]() {
            return pybind11_manual_vectorcall(py_calc);
        }));
#endif
        results.push_back(run_benchmark("pybind11 cpp_function tuple", iterations, 3, [&py_add]() {
            return raw_tuple_call(py_add.ptr());
        }));
        results.push_back(run_benchmark("pybind11 overload tuple 1", iterations, 10, [&pybind11_calc_overload]() {
            return raw_tuple_call1(pybind11_calc_overload.ptr(), 1);
        }));
        results.push_back(run_benchmark("pybind11 overload tuple 2", iterations, 3, [&pybind11_calc_overload]() {
            return raw_tuple_call(pybind11_calc_overload.ptr());
        }));
#if PY_VERSION_HEX >= 0x03080000
        if (PyVectorcall_Function(py_add.ptr()) != nullptr) {
            results.push_back(run_benchmark("pybind11 cpp_function vectorcall", iterations, 3, [&py_add]() {
                return raw_vectorcall(py_add.ptr());
            }));
        }
        if (PyVectorcall_Function(pybind11_calc_overload.ptr()) != nullptr) {
            results.push_back(run_benchmark("pybind11 overload vectorcall 1", iterations, 10, [&pybind11_calc_overload]() {
                return raw_vectorcall1(pybind11_calc_overload.ptr(), 1);
            }));
            results.push_back(run_benchmark("pybind11 overload vectorcall 2", iterations, 3, [&pybind11_calc_overload]() {
                return raw_vectorcall(pybind11_calc_overload.ptr());
            }));
        }
#endif
#endif

#if defined(DYNABRIDGE_HAS_NANOBIND)
        namespace nb = nanobind;
        nb::object nb_calc = nb::borrow<nb::object>(calc);
        nb::object nb_add = nb::cpp_function([](int a, unsigned b) {
            return add_function(a, b);
        });
        nb::module_ nb_main_module = nb::module_::import_("__main__");
        nb_main_module.def("nanobind_calc_overload", [](int a) {
            return a * 10;
        });
        nb_main_module.def("nanobind_calc_overload", [](int a, unsigned b) {
            return add_function(a, b);
        });
        nb::object nanobind_calc_overload = nb_main_module.attr("nanobind_calc_overload");

        results.push_back(run_benchmark("nanobind function call", iterations, 3, [&nb_calc]() {
            return nb::cast<int>(nb_calc(1, 2u));
        }));
#if PY_VERSION_HEX >= 0x03080000
        results.push_back(run_benchmark("nanobind manual vectorcall", iterations, 3, [&nb_calc]() {
            return nanobind_manual_vectorcall(nb_calc);
        }));
#endif
        results.push_back(run_benchmark("nanobind cpp_function tuple", iterations, 3, [&nb_add]() {
            return raw_tuple_call(nb_add.ptr());
        }));
        results.push_back(run_benchmark("nanobind overload tuple 1", iterations, 10, [&nanobind_calc_overload]() {
            return raw_tuple_call1(nanobind_calc_overload.ptr(), 1);
        }));
        results.push_back(run_benchmark("nanobind overload tuple 2", iterations, 3, [&nanobind_calc_overload]() {
            return raw_tuple_call(nanobind_calc_overload.ptr());
        }));
#if PY_VERSION_HEX >= 0x03080000
        if (PyVectorcall_Function(nb_add.ptr()) != nullptr) {
            results.push_back(run_benchmark("nanobind cpp_function vectorcall", iterations, 3, [&nb_add]() {
                return raw_vectorcall(nb_add.ptr());
            }));
        }
        if (PyVectorcall_Function(nanobind_calc_overload.ptr()) != nullptr) {
            results.push_back(run_benchmark("nanobind overload vectorcall 1", iterations, 10, [&nanobind_calc_overload]() {
                return raw_vectorcall1(nanobind_calc_overload.ptr(), 1);
            }));
            results.push_back(run_benchmark("nanobind overload vectorcall 2", iterations, 3, [&nanobind_calc_overload]() {
                return raw_vectorcall(nanobind_calc_overload.ptr());
            }));
        }
#endif
#endif

        print_results(results, iterations);
    } catch (const std::exception& error) {
        std::cerr << "benchmark failed: " << error.what() << '\n';
        Py_Finalize();
        return 1;
    }

    Py_Finalize();
    return 0;
}
