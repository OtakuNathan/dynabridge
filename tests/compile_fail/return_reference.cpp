#include "../../dynabridge/async_import.h"
#include "../../dynabridge/export_callable.h"

#if RETURN_KIND == 1
using result_t = int&;
#elif RETURN_KIND == 2
using result_t = const int&;
#else
using result_t = int&&;
#endif
struct callable { int operator()() const { return 7; } };
struct backend : dynabridge::backend_base<backend> {
    using dynamic_value_t = int;
    template<class T> struct converter {
        template<class Context> static int to(Context&, T value) { return value; }
        template<class Context> static dynabridge::optional<T> from(Context&, int value) { return dynabridge::optional<T>(value); }
    };
    template<class R, class Context> static R invoke_impl(Context&, dynabridge::no_receiver_t) {
        static int value = 1;
        return static_cast<R>(value);
    }
};
struct context { using backend_t = backend; };
struct receiver {};
using contract = dynabridge::free_callable<result_t()>;
int main() {
#if RETURN_PATH == 0
    using signature = dynabridge::signature_from_types_t<result_t>;
    (void)sizeof(signature*);
#elif RETURN_PATH == 1
    (void)sizeof(contract);
#elif RETURN_PATH == 2
    (void)sizeof(dynabridge::export_callable_binder<result_t(), context, callable>);
#elif RETURN_PATH == 3
    (void)sizeof(dynabridge::export_member_callable_binder<receiver, result_t(), context, callable>);
#elif RETURN_PATH == 4
    (void)sizeof(dynabridge::export_overload_candidate<contract, context, callable>);
#elif RETURN_PATH == 5
    (void)sizeof(dynabridge::import_callable<contract>);
#elif RETURN_PATH == 6
    (void)sizeof(dynabridge::import_callable<dynabridge::callable<receiver, result_t()>>);
#elif RETURN_PATH == 7
    (void)sizeof(dynabridge::import_contract_backend_invoker<contract>);
#elif RETURN_PATH == 8
    context ctx;
    (void)backend::invoke<result_t>(ctx, dynabridge::no_receiver_t{});
#elif RETURN_PATH == 9
    using result = typename dynabridge::async_detail::contract_result<contract>::type;
    (void)sizeof(result);
#endif
}
