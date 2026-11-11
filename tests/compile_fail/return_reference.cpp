#include "../../portal/async_import.h"
#include "../../portal/export_callable.h"

#if RETURN_KIND == 1
using result_t = int&;
#elif RETURN_KIND == 2
using result_t = const int&;
#else
using result_t = int&&;
#endif
struct callable { int operator()() const { return 7; } };
struct backend : portal::backend_base<backend> {
    using dynamic_value_t = int;
    template<class T> struct converter {
        template<class Context> static int to(Context&, T value) { return value; }
        template<class Context> static portal::optional<T> from(Context&, int value) { return portal::optional<T>(value); }
    };
    template<class R, class Context> static R invoke_impl(Context&, portal::no_receiver_t) {
        static int value = 1;
        return static_cast<R>(value);
    }
};
struct context { using backend_t = backend; };
struct receiver {};
using contract = portal::free_callable<result_t()>;
int main() {
#if RETURN_PATH == 0
    using signature = portal::signature_from_types_t<result_t>;
    (void)sizeof(signature*);
#elif RETURN_PATH == 1
    (void)sizeof(contract);
#elif RETURN_PATH == 2
    (void)sizeof(portal::export_callable_binder<result_t(), context, callable>);
#elif RETURN_PATH == 3
    (void)sizeof(portal::export_member_callable_binder<receiver, result_t(), context, callable>);
#elif RETURN_PATH == 4
    (void)sizeof(portal::export_overload_candidate<contract, context, callable>);
#elif RETURN_PATH == 5
    (void)sizeof(portal::import_callable<contract>);
#elif RETURN_PATH == 6
    (void)sizeof(portal::import_callable<portal::callable<receiver, result_t()>>);
#elif RETURN_PATH == 7
    (void)sizeof(portal::import_contract_backend_invoker<contract>);
#elif RETURN_PATH == 8
    context ctx;
    (void)backend::invoke<result_t>(ctx, portal::no_receiver_t{});
#elif RETURN_PATH == 9
    using result = typename portal::async_detail::contract_result<contract>::type;
    (void)sizeof(result);
#endif
}
