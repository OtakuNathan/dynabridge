#ifndef PORTAL_ERROR_POLICY_H
#define PORTAL_ERROR_POLICY_H
#ifndef PORTAL_NO_EXCEPTION_STRICT
#define PORTAL_NO_EXCEPTION_STRICT 0
#endif
#include <cassert>
#include <cstdlib>
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#define PORTAL_HAS_EXCEPTIONS 1
#define PORTAL_RAISE(error) throw error
#else
#define PORTAL_HAS_EXCEPTIONS 0
// Legacy must-succeed APIs are contracts. Untrusted input must use the checked
// codec / optional conversion APIs. The error expression is not evaluated.
#define PORTAL_RAISE(error) do { assert(false && #error); std::abort(); } while(false)
#endif
#define PORTAL_NOEXCEPT_IF_DISABLED noexcept(!PORTAL_HAS_EXCEPTIONS)
#endif
