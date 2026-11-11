#ifndef PORTAL_BACKENDS_PYTHON_API_H
#define PORTAL_BACKENDS_PYTHON_API_H

#if defined(_MSC_VER) && defined(_DEBUG) \
        && !defined(Py_DEBUG) \
        && !defined(PORTAL_PYTHON_KEEP_MSVC_DEBUG)
#define PORTAL_PYTHON_RESTORE_MSVC_DEBUG
#undef _DEBUG
#endif

#include <Python.h>

#if defined(PORTAL_PYTHON_RESTORE_MSVC_DEBUG)
#define _DEBUG
#undef PORTAL_PYTHON_RESTORE_MSVC_DEBUG
#endif

#endif //PORTAL_BACKENDS_PYTHON_API_H
