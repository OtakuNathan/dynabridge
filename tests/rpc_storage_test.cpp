#include "portal/portal.h"
#include "portal/backends/rpc.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::abort(); } } while (false)

namespace {
namespace codec = portal::rpc::detail;
using value = portal::rpc::value;
using context = portal::rpc_backend::export_context_t;

template <typename Sequence>
struct sum_arguments;

template <std::size_t... Indices>
struct sum_arguments<std::index_sequence<Indices...>> {
    using signature = int(decltype(static_cast<void>(Indices), int{})...);
    static int call(decltype(static_cast<void>(Indices), int{})... args) noexcept {
        int result = 0;
        using swallow = int[];
        (void)swallow{0, (result += args, 0)...};
        return result;
    }
};

template <std::size_t Count>
void define_sum(portal::rpc::router& router, context& ctx) {
    using sum = sum_arguments<std::make_index_sequence<Count>>;
    const auto name = "sum" + std::to_string(Count);
    router.define(name.c_str(), portal::create_export_callable_binder<typename sum::signature>(ctx, &sum::call));
}

value round_trip(portal::rpc::router& router, const char* name, const std::vector<value>& args) {
    value output;
    std::string error;
    CHECK(codec::try_decode_response(router.dispatch(codec::encode_request(codec::symbol_id(name), args)),
        output, error) == codec::response_status::ok);
    return output;
}

struct legacy_typed_binder {
    using signature_t = int(std::string);
    value* retained;

    portal::optional<value> call_optional(const value& input) {
        *retained = input;
        return portal::optional<value>(value::signed_integer(static_cast<int>(input.string_size())));
    }
};

// Intentionally accepts the exact old accessor type rather than a template.
// A duck-typed binder must never be forced onto the consuming library path.
struct legacy_overload_binder {
    using overloads_t = portal::type_list<>;
    value* retained;

    portal::optional<value> dispatch_optional(std::size_t count, codec::vector_accessor input) {
        CHECK(input.args->size() == count);
        if (count == 0) return {};
        *retained = input.template get<0>();
        std::size_t size = 0;
        for (const auto& arg : *input.args) size += arg.string_size();
        return portal::optional<value>(value::signed_integer(static_cast<int>(size)));
    }
};
}

int main() {
    context ctx;
    portal::rpc::router router;
    define_sum<0>(router, ctx);
    define_sum<1>(router, ctx);
    define_sum<2>(router, ctx);
    define_sum<4>(router, ctx);
    define_sum<5>(router, ctx);
    define_sum<8>(router, ctx);
    for (const std::size_t count : {0, 1, 2, 4, 5, 8}) {
        const auto name = "sum" + std::to_string(count);
        std::vector<value> args;
        for (std::size_t i = 0; i < count; ++i) args.push_back(value::signed_integer(i + 1));
        CHECK(round_trip(router, name.c_str(), args).as_signed() == static_cast<int>(count * (count + 1) / 2));
    }

    int calls = 0;
    const auto check_four = [&calls](std::string a, std::string b, std::string c, std::string d) noexcept {
        ++calls;
        return static_cast<int>(a.size() + b.size() + c.size() + d.size());
    };
    const auto check_five = [&calls](std::string a, std::string b, std::string c, std::string d, std::string e) noexcept {
        ++calls;
        return static_cast<int>(a.size() + b.size() + c.size() + d.size() + e.size());
    };
    router.define("strings4", portal::create_export_callable_binder<int(std::string, std::string, std::string, std::string)>(ctx, check_four));
    router.define("strings5", portal::create_export_callable_binder<int(std::string, std::string, std::string, std::string, std::string)>(ctx, check_five));
    for (const std::size_t count : {4, 5}) {
        const auto name = "strings" + std::to_string(count);
        std::vector<value> args(count, value::string(std::string(33, 'x')));
        auto frame = codec::encode_request(codec::symbol_id(name.c_str()), args);
        for (std::size_t size = 0; size < frame.size(); ++size) {
            value output;
            std::string error;
            CHECK(codec::try_decode_response(router.dispatch(portal::rpc::bytes(frame.begin(), frame.begin() + size)),
                output, error) == codec::response_status::remote_error);
            CHECK(error == "invalid RPC request frame");
        }
        CHECK(calls == static_cast<int>(count - 4));
        CHECK(round_trip(router, name.c_str(), args).as_signed() == static_cast<int>(33 * count));
    }
    CHECK(calls == 2);

    value retained;
    router.define("legacy", legacy_typed_binder{&retained});
    router.define("legacy_group", legacy_overload_binder{&retained});
    const std::string text(4096, 'l');
    CHECK(round_trip(router, "legacy", {value::string(text)}).as_signed() == static_cast<int>(text.size()));
    CHECK(retained.copy_string() == text);
    for (const std::size_t count : {1, 4, 5, 8}) {
        CHECK(round_trip(router, "legacy_group", std::vector<value>(count, value::string(text))).as_signed()
            == static_cast<int>(count * text.size()));
        CHECK(retained.copy_string() == text); // The decoded frame has already died.
    }

    // Nested calls vary their arity while outer owning arguments remain alive.
    auto nested = [&router](const std::string& input, int depth) {
        if (depth == 0) return input;
        const bool wide = (depth & 1) != 0;
        std::vector<value> args{value::string(input), value::signed_integer(depth - 1),
            value::signed_integer(2), value::signed_integer(3)};
        if (wide) args.push_back(value::signed_integer(4));
        auto inner = round_trip(router, wide ? "nested5" : "nested4", args);
        CHECK(inner.copy_string() == input);
        return input;
    };
    router.define("nested4", portal::create_export_callable_binder<std::string(const std::string&, int, int, int)>(ctx,
        [&nested](const std::string& input, int depth, int, int) noexcept { return nested(input, depth); }));
    router.define("nested5", portal::create_export_callable_binder<std::string(const std::string&, int, int, int, int)>(ctx,
        [&nested](const std::string& input, int depth, int, int, int) noexcept { return nested(input, depth); }));
    CHECK(round_trip(router, "nested4", {value::string(text), value::signed_integer(8),
        value::signed_integer(2), value::signed_integer(3)}).copy_string() == text);

    router.define("echo", portal::create_export_callable_binder<std::string(std::string)>(ctx,
        [](std::string input) noexcept { return input; }));
    std::atomic<unsigned> completed{0};
    std::vector<std::thread> workers;
    for (unsigned worker = 0; worker < 4; ++worker) {
        workers.emplace_back([worker, &router, &completed] {
            const std::string input(128 + worker, static_cast<char>('a' + worker));
            for (unsigned i = 0; i < 200; ++i) {
                CHECK(round_trip(router, "echo", {value::string(input)}).copy_string() == input);
                CHECK(round_trip(router, "sum4", {value::signed_integer(worker), value::signed_integer(i),
                    value::signed_integer(2), value::signed_integer(3)}).as_signed() == worker + i + 5);
                CHECK(round_trip(router, "sum5", {value::signed_integer(worker), value::signed_integer(i),
                    value::signed_integer(2), value::signed_integer(3), value::signed_integer(4)}).as_signed() == worker + i + 9);
            }
            completed.fetch_add(1, std::memory_order_relaxed);
        });
    }
    for (auto& worker : workers) worker.join();
    CHECK(completed.load(std::memory_order_relaxed) == 4);
    std::puts("RPC owning argument, legacy, nested and concurrent tests passed");
}
