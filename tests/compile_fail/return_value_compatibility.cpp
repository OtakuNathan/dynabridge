#include "../../dynabridge/export_callable.h"
struct immovable {
    immovable() = default;
    immovable(const immovable&) = delete;
    immovable(immovable&&) = delete;
};
struct backend {
    using dynamic_value_t = int;
    template<class T> struct converter;
};
template<> struct backend::converter<immovable> {
    template<class Context> static int to(Context&, immovable&&) noexcept { return 7; }
};
struct context { using backend_t = backend; };
int main() {
    context ctx;
    auto binder = dynabridge::create_export_callable_binder<immovable()>(ctx,
        []() -> immovable { return immovable{}; });
    return binder() != 7;
}
