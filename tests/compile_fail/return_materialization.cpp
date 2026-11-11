#include <string>
#include "../../portal/export_callable.h"
#include "../../portal/backends/rpc.h"
int main() {
    portal::rpc_backend::export_context_t ctx;
#if RETURN_COPY
    const std::string value = "copy";
    auto binder = portal::create_export_callable_binder<std::string()>(ctx,
        [&]() -> const std::string& { return value; });
#else
    auto binder = portal::create_export_callable_binder<std::string()>(ctx,
        []() -> std::string { return "move"; });
#endif
    return binder().string_size() != 4;
}
