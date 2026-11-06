#include "../../dynabridge/bridge.h"
#include "../../dynabridge/backends/rpc.h"
struct payload { int n; };
namespace dynabridge {
template<> struct rpc_backend::converter<payload> {
#if defined(PROBE_WRONG_RETURN)
    template<class C> static int probe(C&, const rpc::value&) noexcept { return 1; }
#elif defined(PROBE_NOT_NOEXCEPT)
    template<class C> static bool probe(C&, const rpc::value&) { return true; }
#elif !defined(PROBE_MISSING)
    template<class C> static bool probe(C&, const rpc::value&) noexcept { return true; }
#endif
    template<class C> static optional<payload> from(C&, const rpc::value&) noexcept { return optional<payload>(payload{1}); }
};
}
int main() {
    dynabridge::rpc_backend::export_context_t ctx;
    auto binder = dynabridge::create_export_callable_binder<int(payload)>(ctx, [](payload p) noexcept { return p.n; });
    return int(binder(dynabridge::rpc::value::signed_integer(1)).as_signed()) - 1;
}
