#ifndef DYNABRIDGE_BACKENDS_PYTHON_CONVERTERS_H
#define DYNABRIDGE_BACKENDS_PYTHON_CONVERTERS_H

#include <limits>
#include <stdexcept>

namespace dynabridge {
    namespace py_backend_detail {
        inline bool compact_long_value(PyObject* value, Py_ssize_t& result) noexcept {
            if (!PyLong_CheckExact(value)) {
                return false;
            }

#if !defined(Py_LIMITED_API) && !defined(PYPY_VERSION) && PY_VERSION_HEX >= 0x030C0000
            auto* long_value = reinterpret_cast<PyLongObject*>(value);
            if (PyUnstable_Long_IsCompact(long_value)) {
                result = PyUnstable_Long_CompactValue(long_value);
                return true;
            }
#elif !defined(Py_LIMITED_API) && !defined(PYPY_VERSION)
            const Py_ssize_t size = Py_SIZE(value);
            if (size >= -1 && size <= 1) {
                auto* long_value = reinterpret_cast<PyLongObject*>(value);
                result = size == 0
                    ? 0
                    : (size > 0
                        ? static_cast<Py_ssize_t>(long_value->ob_digit[0])
                        : -static_cast<Py_ssize_t>(long_value->ob_digit[0]));
                return true;
            }
#endif

            return false;
        }
    }

    template <>
    struct py_backend::converter<int> {
        static PyObject* to(context_t&, int value) {
            PyObject* result = PyLong_FromLong(value);
            if (result == nullptr) {
                throw std::runtime_error("dynabridge Python int conversion failed");
            }
            return result;
        }

        static optional<int> from(context_t&, PyObject* value) {
            Py_ssize_t compact = 0;
            if (py_backend_detail::compact_long_value(value, compact)) {
                if (compact < static_cast<Py_ssize_t>(std::numeric_limits<int>::min())
                        || compact > static_cast<Py_ssize_t>(std::numeric_limits<int>::max())) {
                    return optional<int>();
                }
                return optional<int>(static_cast<int>(compact));
            }
            if (!PyLong_Check(value)) {
                return optional<int>();
            }

            int overflow = 0;
            const long result = PyLong_AsLongAndOverflow(value, &overflow);
            if (overflow != 0 || PyErr_Occurred()) {
                PyErr_Clear();
                return optional<int>();
            }
            if (result < static_cast<long>(std::numeric_limits<int>::min())
                    || result > static_cast<long>(std::numeric_limits<int>::max())) {
                return optional<int>();
            }
            return optional<int>(static_cast<int>(result));
        }
    };

    template <>
    struct py_backend::converter<unsigned> {
        static PyObject* to(context_t&, unsigned value) {
            PyObject* result = PyLong_FromUnsignedLong(value);
            if (result == nullptr) {
                throw std::runtime_error("dynabridge Python unsigned conversion failed");
            }
            return result;
        }

        static optional<unsigned> from(context_t&, PyObject* value) {
            Py_ssize_t compact = 0;
            if (py_backend_detail::compact_long_value(value, compact)) {
                if (compact < 0
                        || static_cast<unsigned long>(compact)
                            > static_cast<unsigned long>(std::numeric_limits<unsigned>::max())) {
                    return optional<unsigned>();
                }
                return optional<unsigned>(static_cast<unsigned>(compact));
            }
            if (!PyLong_Check(value)) {
                return optional<unsigned>();
            }

            const unsigned long result = PyLong_AsUnsignedLong(value);
            if (PyErr_Occurred()) {
                PyErr_Clear();
                return optional<unsigned>();
            }
            if (result > static_cast<unsigned long>(std::numeric_limits<unsigned>::max())) {
                return optional<unsigned>();
            }
            return optional<unsigned>(static_cast<unsigned>(result));
        }
    };
}

#endif //DYNABRIDGE_BACKENDS_PYTHON_CONVERTERS_H
