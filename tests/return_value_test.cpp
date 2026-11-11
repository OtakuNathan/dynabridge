#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "portal/bridge.h"
#include "portal/backends/rpc.h"

namespace {
using value = portal::rpc::value;
using context = portal::rpc_backend::export_context_t;
using signature = std::string(const std::string&);
using overloads = portal::type_list<portal::free_callable<signature>>;

void check(bool condition) { if (!condition) std::abort(); }
const std::string& echo_ref(const std::string& text) { return text; }
std::string&& echo_rref(std::string&& text) { return std::move(text); }
const std::string echo_const(const std::string& text) { return text; }

struct native_receiver {
    const std::string& echo(const std::string& text) { return text; }
};
struct receiver {
    native_receiver& native() { return object; }
    native_receiver object;
};
struct member_backend : portal::backend_base<member_backend> {
    using dynamic_value_t = value;
    template<class T> struct converter : portal::rpc_backend::converter<T> {};
    template<class Class> struct bound_object {
        Class* object;
        template<class Context> Class* native(Context&) { return object; }
    };
    template<class Class, class Context>
    static bound_object<Class> bind_export_object_impl(Context& ctx, const value&) {
        return {ctx.object};
    }
};
struct member_context {
    using backend_t = member_backend;
    receiver* object;
};

struct move_only {
    move_only() = default;
    move_only(const move_only&) = delete;
    move_only(move_only&&) noexcept = default;
};
struct counted {
    int* copies;
    int* moves;
    counted(int& c, int& m) : copies(&c), moves(&m) {}
    counted(const counted& other) : copies(other.copies), moves(other.moves) { ++*copies; }
    counted(counted&& other) noexcept : copies(other.copies), moves(other.moves) { ++*moves; }
};
struct explicit_result { explicit operator int() const { return 7; } };
struct explicit_callable { explicit_result operator()() const { return {}; } };
struct conversion_backend {
    using dynamic_value_t = value;
    template<class T> struct converter;
};
template<> struct conversion_backend::converter<move_only> {
    template<class Context> static value to(Context&, move_only&&) { return value::boolean(true); }
};
template<> struct conversion_backend::converter<counted> {
    template<class Context> static value to(Context&, counted&&) { return value::boolean(true); }
};
struct conversion_context { using backend_t = conversion_backend; };
struct returns_lvalue { move_only& operator()(); };
struct returns_rvalue { move_only&& operator()(); };

static_assert(portal::is_export_callable_bindable<signature, context, decltype(&echo_ref)>::value,
    "actual const& results must work with a declared value result");
static_assert(!portal::is_export_callable_bindable<const std::string&(const std::string&), context, decltype(&echo_ref)>::value,
    "declared const& results must be unbindable");
static_assert(!portal::is_export_callable_bindable<std::string&&(std::string&&), context, decltype(&echo_rref)>::value,
    "declared && results must be unbindable");
static_assert(!portal::is_export_callable_bindable<move_only(), conversion_context, returns_lvalue>::value,
    "a native lvalue result cannot materialize an uncopyable declared value");
static_assert(portal::is_export_callable_bindable<move_only(), conversion_context, returns_rvalue>::value,
    "a native rvalue result may materialize a move-only declared value");
static_assert(!portal::is_export_callable_bindable<int(), context, explicit_callable>::value,
    "explicit-only conversions must not widen the declared return contract");
static_assert(!portal::is_forward_result_convertible<context, const std::string&>::value,
    "import result probes reject references");
static_assert(!portal::is_export_member_callable_bindable<receiver, const std::string&(const std::string&),
    member_context, decltype(&native_receiver::echo)>::value, "member result probes reject references");
}

int main() {
    context ctx;
    auto direct = portal::create_export_callable_binder<signature>(ctx, &echo_ref);
    auto overloaded = portal::create_export_overload_binder<overloads>(ctx, &echo_ref);
    auto moved = portal::create_export_callable_binder<std::string(std::string&&)>(ctx, &echo_rref);
    auto constant = portal::create_export_callable_binder<const std::string(const std::string&)>(ctx, &echo_const);
    receiver object;
    member_context member_ctx{&object};
    auto member = portal::create_export_member_callable_binder<receiver, signature>(member_ctx, &native_receiver::echo);
    auto member_lambda = portal::create_export_member_callable_binder<receiver, signature>(member_ctx,
        [](receiver&, const std::string& text) -> const std::string& { return text; });

    portal::rpc::router router;
    router.define("direct", direct);
    router.define("overloaded", overloaded);
    router.define("moved", moved);
    router.define("constant", constant);
    const std::vector<std::string> cases{"", "short", std::string(8192, 'x'), std::string("a\0b\0c", 5)};
    unsigned checks = 0;
    for (unsigned repeat = 0; repeat < 100; ++repeat) {
        for (const auto& text : cases) {
            // Inspect AFTER the binder's converted argument tuple is destroyed.
            auto result = direct(value::string(text));
            check(result.copy_string() == text); ++checks;
            auto optional = direct.call_optional(value::string(text));
            check(optional && optional->copy_string() == text); ++checks;
            result = overloaded(value::string(text));
            check(result.copy_string() == text); ++checks;
            optional = overloaded.call_optional(value::string(text));
            check(optional && optional->copy_string() == text); ++checks;
            result = member(value{}, value::string(text));
            check(result.copy_string() == text); ++checks;
            result = member_lambda(value{}, value::string(text));
            check(result.copy_string() == text); ++checks;
            for (const char* name : {"direct", "overloaded", "moved", "constant"}) {
                auto request = portal::rpc::detail::encode_request_values(
                    portal::rpc::detail::symbol_id(name), value::borrowed_string(text));
                const auto saved = request;
                auto response = portal::rpc::detail::decode_response(router.dispatch(request));
                check(response.copy_string() == text && request == saved); ++checks;
            }
        }
    }
    std::string storage(8192, 's');
    auto external = portal::create_export_callable_binder<std::string()>(ctx,
        [&]() -> const std::string& { return storage; });
    auto snapshot = external();
    storage.assign(8192, 't');
    check(snapshot.copy_string() == std::string(8192, 's')); ++checks;

    // Generic input lowering keeps its previous borrowing behavior.
    auto borrowed = portal::to_cast<const std::string&>(ctx, storage);
    check(borrowed.string_data() == storage.data()); ++checks;
    check(!direct.call_optional(value::boolean(true))); ++checks;
    auto discarded = portal::create_export_callable_binder<void(const std::string&)>(ctx, &echo_ref);
    discarded(value::string("ignored native result")); ++checks;
    conversion_context conversion_ctx;
    move_only owned;
    auto move_result = portal::create_export_callable_binder<move_only()>(conversion_ctx,
        [&]() -> move_only&& { return std::move(owned); });
    check(move_result().as_boolean()); ++checks;
    int copies = 0, moves = 0;
    counted counter(copies, moves);
    auto count_rvalue = portal::create_export_callable_binder<counted()>(conversion_ctx,
        [&]() -> counted&& { return std::move(counter); });
    check(count_rvalue().as_boolean() && copies == 0 && moves == 0); ++checks;
    auto count_reference = portal::create_export_callable_binder<counted()>(conversion_ctx,
        [&]() -> const counted& { return counter; });
    check(count_reference().as_boolean() && copies == 1 && moves == 0); ++checks;
    std::printf("return value lifetime: %u checks passed\n", checks);
}
