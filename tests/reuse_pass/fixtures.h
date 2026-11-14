#ifndef PORTAL_REUSE_TEST_FIXTURES_H
#define PORTAL_REUSE_TEST_FIXTURES_H
#ifdef PORTAL_REUSE_ISOLATED
#include "isolated_rpc.h"
#else
#include "portal/export_callable.h"
#include "portal/backends/rpc.h"
#endif
#ifndef PORTAL_REUSE_BASELINE
#include "portal/backends/rpc_reuse.h"
#endif
#include <functional>
#include <string>
#include <utility>

// Exercise the supported legacy Binder ABI, identically in both builds. These
// fixtures do not claim to benchmark generated-overload selection/conversion.
namespace reuse_test {
using value = portal::rpc::value;
using result = portal::optional<value>;
struct add {
    using signature_t = int(int, int);
    result call_optional(const value& a, const value& b) {
        if (a.kind() != portal::rpc::value_kind::signed_integer ||
            b.kind() != portal::rpc::value_kind::signed_integer) return {};
        return value::signed_integer(a.as_signed() + b.as_signed());
    }
};
struct echo {
    using signature_t = std::string(std::string);
    result call_optional(value& a) {
        if (a.kind() != portal::rpc::value_kind::string) return {};
        return value::string(a.take_string());
    }
};
struct length {
    using signature_t = int(std::string);
    result call_optional(const value& a) {
        if (a.kind() != portal::rpc::value_kind::string) return {};
        return value::unsigned_integer(a.string_size());
    }
};
struct callback {
    using signature_t = int(int);
    std::function<result(value&)> fn;
    result call_optional(value& a) { return fn(a); }
};
}
#endif
