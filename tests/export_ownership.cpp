#include <portal/export_callable.h>

#include <cstdio>
#include <cstdlib>
#include <tuple>
#include <utility>

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "export ownership failed at line %d: %s\n", __LINE__, #condition); \
    std::abort(); \
} } while (false)

namespace {
struct counts {
    unsigned copies = 0;
    unsigned input_moves = 0;
    unsigned result_copies = 0;
};

template<bool NothrowMove>
struct dynamic_value {
    int number;
    counts* events;
    bool input;
    dynamic_value(int n, counts& e, bool i = false) noexcept
        : number(n), events(&e), input(i) {}
    dynamic_value(const dynamic_value& other) noexcept
        : number(other.number), events(other.events), input(other.input) {
        ++events->copies;
        if (!input) ++events->result_copies;
    }
    dynamic_value(dynamic_value&& other) noexcept(NothrowMove)
        : number(other.number), events(other.events), input(other.input) {
        if (input) ++events->input_moves;
        other.number = -1;
    }
};

// A custom value can be copied without defining a move constructor at all.
struct copy_only_value {
    int number;
    counts* events;
    copy_only_value(int n, counts& e, bool = false) noexcept : number(n), events(&e) {}
    copy_only_value(const copy_only_value& other)
        : number(other.number), events(other.events) { ++events->copies; }
};

template<class Dynamic>
struct backend {
    using dynamic_value_t = Dynamic;
    struct context {
        using backend_t = backend;
        counts events;
    };
    template<class T> struct converter {
        static portal::optional<T> from(context&, const Dynamic& value) noexcept {
            if (value.number < 0) return {};
            return portal::optional<T>(static_cast<T>(value.number));
        }
        static Dynamic to(context& ctx, T value) noexcept {
            return Dynamic(static_cast<int>(value), ctx.events);
        }
    };
};

template<class Dynamic>
void check_copy_compatibility() {
    using B = backend<Dynamic>;
    typename B::context ctx;
    using overloads = portal::type_list<portal::free_callable<int(int)>>;
    auto binder = portal::create_export_overload_binder<overloads>(ctx,
        [](int value) noexcept { return value + 1; });
    Dynamic input(7, ctx.events, true);
    auto result = binder(input);
    CHECK(result.number == 8 && input.number == 7);
    auto optional = binder.call_optional(input);
    CHECK(optional && optional->number == 8 && input.number == 7);
    auto arguments = std::make_tuple(input);
    auto dispatched = binder.dispatch(1,
        portal::tuple_export_arg_accessor<decltype(arguments)>{&arguments});
    CHECK(dispatched.number == 8 && std::get<0>(arguments).number == 7);
}

void owning_value_moves() {
    using Dynamic = dynamic_value<true>;
    using B = backend<Dynamic>;
    B::context ctx;
    using overloads = portal::type_list<portal::free_callable<int(int)>>;
    auto binder = portal::create_export_overload_binder<overloads>(ctx,
        [](int value) noexcept { return value + 1; });
    Dynamic input(7, ctx.events, true);
    auto result = binder(input);
    CHECK(result.number == 8 && input.number == 7);
    CHECK(ctx.events.copies == 1 && ctx.events.result_copies == 0);
    ctx.events = {};
    auto optional = binder.call_optional(input);
    CHECK(optional && optional->number == 8 && input.number == 7);
    CHECK(ctx.events.copies == 1 && ctx.events.result_copies == 0);
    auto arguments = std::make_tuple(input);
    ctx.events = {};
    auto dispatched = binder.dispatch(1,
        portal::tuple_export_arg_accessor<decltype(arguments)>{&arguments});
    CHECK(dispatched.number == 8 && ctx.events.copies == 0);
}

void throwing_moves_keep_copy_fallback() {
    using Dynamic = dynamic_value<false>;
    using B = backend<Dynamic>;
    B::context ctx;
    using overloads = portal::type_list<portal::free_callable<int(int)>>;
    auto binder = portal::create_export_overload_binder<overloads>(ctx,
        [](int value) noexcept { return value + 1; });
    Dynamic input(7, ctx.events, true);
    auto result = binder(input);
    CHECK(result.number == 8 && input.number == 7);
    CHECK(ctx.events.input_moves == 0 && ctx.events.result_copies >= 1);
}

struct ordered_callable {
    int operator()(int value) const noexcept { return value + 100; }
    int operator()(unsigned value) const noexcept { return static_cast<int>(value) + 200; }
};

void first_viable_order_is_unchanged() {
    using Dynamic = dynamic_value<true>;
    using B = backend<Dynamic>;
    B::context ctx;
    using overloads = portal::type_list<portal::free_callable<int(unsigned)>,
        portal::free_callable<int(int)>>;
    auto binder = portal::create_export_overload_binder<overloads>(ctx, ordered_callable{});
    Dynamic input(7, ctx.events, true);
    CHECK(binder(input).number == 207 && input.number == 7);
    Dynamic rejected(-1, ctx.events, true);
    CHECK(!binder.call_optional(rejected));
    CHECK(rejected.number == -1);
}
}

int main() {
    owning_value_moves();
    check_copy_compatibility<copy_only_value>();
    check_copy_compatibility<dynamic_value<false>>();
    throwing_moves_keep_copy_fallback();
    first_viable_order_is_unchanged();
    std::puts("export ownership contracts passed");
}
