#ifndef DYNABRIDGE_ERROR_POLICY_H
#define DYNABRIDGE_ERROR_POLICY_H
#ifndef DYNABRIDGE_NO_EXCEPTION_STRICT
#define DYNABRIDGE_NO_EXCEPTION_STRICT 0
#endif
#include <cassert>
#include <cstdlib>
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#define DYNABRIDGE_HAS_EXCEPTIONS 1
#define DYNABRIDGE_RAISE(error) throw error
#else
#define DYNABRIDGE_HAS_EXCEPTIONS 0
// Legacy must-succeed APIs are contracts. Untrusted input must use the checked
// codec / optional conversion APIs. The error expression is not evaluated.
#define DYNABRIDGE_RAISE(error) do { assert(false && #error); std::abort(); } while(false)
#endif
#define DYNABRIDGE_NOEXCEPT_IF_DISABLED noexcept(!DYNABRIDGE_HAS_EXCEPTIONS)
#endif
