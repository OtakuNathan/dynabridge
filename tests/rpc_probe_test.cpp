// Standalone: g++ -std=c++14 -I. tests/rpc_probe_test.cpp -o /tmp/probe-contract && /tmp/probe-contract
// Repeat with -fno-exceptions -DPORTAL_NO_EXCEPTION_STRICT=0 and =1.
#include "portal/portal.h"
#include "portal/backends/rpc.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <functional>
#if defined(__unix__)
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#endif
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::abort(); } } while(false)
using V = portal::rpc::value;
using C = portal::rpc_backend::export_context_t;
namespace codec = portal::rpc::detail;
static int probes, conversions, natives, fallbacks, alive, created, destroyed, mode, required_probes;
static const char* consumed_address;
struct prepared {
    std::unique_ptr<std::string> text;
    explicit prepared(std::string s) : text(new std::string(std::move(s))) { ++alive; ++created; }
    prepared(prepared&& p) noexcept : text(std::move(p.text)) {}
    prepared(const prepared&) = delete;
    ~prepared() { if (text) { --alive; ++destroyed; } }
};
namespace portal {
template<> struct rpc_backend::converter<prepared> {
    template<class Context> static bool probe(Context&, const V& v) noexcept {
        ++probes; // Test instrumentation only: production probes must have no user effects.
        return v.kind() == rpc::value_kind::string && v.string_size() != 0;
    }
    template<class Context, class Dynamic>
    static optional<prepared> from(Context& c, Dynamic&& v) PORTAL_NOEXCEPT_IF_DISABLED {
        CHECK(probes >= required_probes);
        ++conversions;
        if (mode == 1 || v.kind() != rpc::value_kind::string || v.string_size() == 0) return {};
#if PORTAL_HAS_EXCEPTIONS
        if (mode == 2) throw std::runtime_error("converter failure");
        if (mode == 4 && conversions == 2) throw std::runtime_error("late converter failure");
#endif
        auto s = rpc_backend::converter<std::string>::from(c, std::forward<Dynamic>(v));
        consumed_address = s->data();
        return optional<prepared>(prepared(std::move(*s)));
    }
};
}
// A custom descriptor export_argument uses the old try_make/get API only.
// Its preparation may run foreign user code and must not be repeated at commit.
struct descriptor_tag {};
using descriptor = portal::object_param<descriptor_tag, portal::export_t>;
struct custom_scalar {};
static int descriptor_calls, scalar_calls;
static V* descriptor_mutation;
static bool descriptor_reject, descriptor_mutate_to_valid;
namespace portal {
template<> struct export_argument<descriptor, C> {
    using cpp_type = prepared&;
    using optional_t = optional<prepared>;
    static optional_t try_make(C&, const V&) {
        ++descriptor_calls;
        if (descriptor_mutation) {
            *descriptor_mutation = descriptor_mutate_to_valid ? V::string("payload") : V::boolean(true);
        }
        if (descriptor_reject) return {};
        return optional_t(prepared("descriptor"));
    }
    static prepared& get(C&, optional_t& value) { return *value; }
};
template<> struct export_argument<custom_scalar, C> {
    using cpp_type = int;
    using optional_t = optional<int>;
    static bool probe(C&, const V& v) noexcept { return v.kind() == rpc::value_kind::signed_integer; }
    static optional_t try_make(C&, const V& v) noexcept { ++scalar_calls; return optional_t(static_cast<int>(v.as_signed())); }
    static int get(C&, optional_t& value) noexcept { return *value; }
};
}
static void reset() { CHECK(alive == 0); probes = conversions = natives = fallbacks = created = destroyed = mode = required_probes = 0; consumed_address = nullptr; }
struct selection_callable {
    int operator()(prepared p, int) const noexcept { ++natives; CHECK(*p.text == "payload"); return 1; }
    int operator()(const prepared& p, unsigned) const noexcept { ++natives; CHECK(*p.text == "payload"); return 2; }
};
using SI = portal::free_callable<int(prepared, int)>;
using SU = portal::free_callable<int(const prepared&, unsigned)>;
struct terminal {
    int operator()(prepared) const PORTAL_NOEXCEPT_IF_DISABLED {
        ++natives;
#if PORTAL_HAS_EXCEPTIONS
        if (mode == 3) throw std::runtime_error("native failure");
#endif
        return 1;
    }
    int operator()(std::string) const noexcept { ++fallbacks; return 2; }
};
struct alias_terminal {
    int operator()(prepared, prepared) const noexcept { ++natives; return 1; }
    int operator()(std::string, std::string) const noexcept { ++fallbacks; return 2; }
};
using AliasList = portal::type_list<portal::free_callable<int(prepared, prepared)>, portal::free_callable<int(std::string, std::string)>>;
using TerminalList = portal::type_list<portal::free_callable<int(prepared)>, portal::free_callable<int(std::string)>>;
struct transport {
    V response;
    portal::rpc::bytes round_trip(const portal::rpc::bytes&) { return codec::encode_response(response); }
};
// Legacy duck-type binders intentionally have only the const-value call API.
struct legacy_binder {
    using signature_t = int(std::string);
    portal::optional<V> call_optional(const V& v) { return portal::optional<V>(V::signed_integer(static_cast<int>(v.string_size()))); }
};
// Merely sharing the wire value does not opt a backend into probes.
struct old_value { int number; };
struct old_backend : portal::backend_base<old_backend> {
    using dynamic_value_t = V;
    template<class T> struct converter : portal::rpc_backend::converter<T> {};
    template<class Context> static V undefined(Context&) noexcept { return {}; }
};
static int old_attempts;
template<> struct old_backend::converter<old_value> {
    template<class Context> static portal::optional<old_value> from(Context&, const V&) noexcept {
        ++old_attempts; return {};
    }
};
struct old_context { using backend_t = old_backend; };
struct old_callable {
    int operator()(old_value) const noexcept { std::abort(); }
    int operator()(std::string s) const noexcept { return int(s.size()); }
};
struct legacy_overload_binder {
    using overloads_t = portal::type_list<>;
    template<class Accessor>
    portal::optional<V> dispatch_optional(std::size_t count, Accessor accessor) {
        static_assert(std::is_same<decltype(accessor.template get<0>()), const V&>::value,
            "custom binders must keep const access");
        return count == 1 ? portal::optional<V>(V::signed_integer(int(accessor.template get<0>().string_size()))) : portal::optional<V>{};
    }
};
int main() {
    old_context old;
    auto legacy = portal::create_export_overload_binder<portal::type_list<
        portal::free_callable<int(old_value)>, portal::free_callable<int(std::string)>>>(old, old_callable{});
    CHECK(legacy(V::string("abc")).as_signed() == 3 && old_attempts == 1);
    CHECK(!portal::from_optional<old_value>(old, V::boolean(true)) && old_attempts == 2);
    static_assert(!portal::is_transactional_export_binder<decltype(legacy)>::value, "explicit policy required");
    C c;
    reset();
    V bad = V::string("");
    CHECK(!portal::from_optional<prepared>(c, bad));
    CHECK(probes == 0 && conversions == 1 && bad.string_size() == 0);
    V good = V::string("payload");
    CHECK(portal::from_probe<prepared>(c, good));
    CHECK(conversions == 1 && good.copy_string() == "payload");
    { auto p = portal::from_optional<prepared>(c, static_cast<const V&>(good)); CHECK(p && *p->text == "payload"); }
    CHECK(probes == 1 && conversions == 2 && created == destroyed && good.copy_string() == "payload");

    reset();
    auto group = portal::create_export_overload_binder<portal::type_list<SI, SU>>(c, selection_callable{});
    std::vector<V> args{V::string("payload"), V::unsigned_integer(7)};
    auto result = group.dispatch_optional(args.size(), codec::consuming_vector_accessor{&args});
    CHECK(result && result->as_signed() == 2 && conversions == 1 && natives == 1);
    CHECK(created == destroyed && alive == 0);
    // The first candidate must never prepare its first argument before rejecting the second.
    CHECK(probes == 2);
    reset();
    args = {V::string("payload"), V::boolean(true)};
    CHECK(!group.dispatch_optional(args.size(), codec::consuming_vector_accessor{&args}));
    CHECK(conversions == 0 && natives == 0 && args[0].copy_string() == "payload");
    args = {V::string(""), V::unsigned_integer(7)};
    CHECK(!group.dispatch_optional(args.size(), codec::consuming_vector_accessor{&args}));
    CHECK(conversions == 0 && natives == 0 && args[1].as_unsigned() == 7);
    int before = probes;
    CHECK(!group.dispatch_optional(0, codec::consuming_vector_accessor{&args}));
    CHECK(!group.dispatch_optional(3, codec::consuming_vector_accessor{&args}));
    CHECK(probes == before);
    auto empty = portal::create_export_overload_binder<portal::type_list<>>(c, selection_callable{});
    CHECK(!empty.dispatch_optional(args.size(), codec::consuming_vector_accessor{&args}));

    reset();
    // A first-candidate match does not probe or convert later candidates.
    args = {V::string("payload"), V::signed_integer(7)};
    result = group.dispatch_optional(args.size(), codec::consuming_vector_accessor{&args});
    CHECK(result && result->as_signed() == 1 && probes == 1 && conversions == 1 && natives == 1);
    reset();
    auto ordered = portal::create_export_overload_binder<TerminalList>(c, terminal{});
    args = {V::string("payload")};
    result = ordered.dispatch_optional(1, codec::consuming_vector_accessor{&args});
    CHECK(result && result->as_signed() == 1 && probes == 1 && conversions == 1 && fallbacks == 0);
    reset();
    auto rvalue = portal::create_export_callable_binder<int(prepared&&)>(c,
        [](prepared&& p) noexcept { ++natives; return static_cast<int>(p.text->size()); });
    V input = V::string(std::string(4096, 'x'));
    const char* original = input.string_data();
    CHECK(rvalue(input).as_signed() == 4096);
    CHECK(input.string_data() == original && input.string_size() == 4096); // direct binder copies
    CHECK(probes == 1 && conversions == 1 && alive == 0 && created == destroyed);
    reset();
    args = {V::string(std::string(4096, 'x'))};
    original = args[0].string_data();
    CHECK(rvalue.dispatch_optional(1, codec::consuming_vector_accessor{&args}));
    CHECK(consumed_address == original && alive == 0 && created == destroyed);
    reset();
    std::string borrowed(4096, 'b');
    args = {V::borrowed_string(borrowed)};
    CHECK(rvalue.dispatch_optional(1, codec::consuming_vector_accessor{&args}));
    CHECK(borrowed == std::string(4096, 'b') && consumed_address != borrowed.data());
    CHECK(probes == 1 && conversions == 1 && created == destroyed);

    reset();
    auto three = portal::create_export_callable_binder<int(prepared, const prepared&, prepared&&)>(c,
        [](prepared a, const prepared& b, prepared&& d) noexcept {
            ++natives;
            CHECK(*a.text == "payload" && *b.text == "payload" && *d.text == "payload");
            return 3;
        });
    args = {V::string("payload"), V::string("payload"), V::string("payload")};
    required_probes = 3;
    result = three.dispatch_optional(3, codec::consuming_vector_accessor{&args});
    CHECK(result && result->as_signed() == 3 && probes == 3 && conversions == 3 && natives == 1);
    CHECK(alive == 0 && created == 3 && destroyed == 3);

    reset();
    auto descriptor_binder = portal::create_export_callable_binder<int(prepared, descriptor)>(c,
        [](prepared p, prepared& handle) noexcept {
            ++natives;
            CHECK(*p.text == "payload" && *handle.text == "descriptor");
            return 9;
        });
    descriptor_calls = 0;
    args = {V::boolean(true), V::signed_integer(1)};
    descriptor_mutation = &args[0]; descriptor_mutate_to_valid = true;
    result = descriptor_binder.dispatch_optional(2, codec::consuming_vector_accessor{&args});
    CHECK(result && result->as_signed() == 9 && descriptor_calls == 1 && conversions == 1);
    CHECK(natives == 1 && created == 2 && destroyed == 2 && alive == 0);
    reset(); descriptor_calls = 0;
    args = {V::string("payload"), V::signed_integer(1)};
    descriptor_mutation = &args[0]; descriptor_mutate_to_valid = false;
    CHECK(!descriptor_binder.dispatch_optional(2, codec::consuming_vector_accessor{&args}));
    CHECK(descriptor_calls == 1 && conversions == 0 && natives == 0);
    CHECK(created == 1 && destroyed == 1 && alive == 0);
    reset(); descriptor_calls = 0; descriptor_mutation = nullptr; descriptor_reject = true;
    args = {V::string("payload"), V::signed_integer(1)};
    CHECK(!descriptor_binder.dispatch_optional(2, codec::consuming_vector_accessor{&args}));
    CHECK(descriptor_calls == 1 && conversions == 0 && natives == 0 && created == 0);
    descriptor_reject = false;
    reset(); descriptor_calls = 0;
    using DescriptorContract = portal::free_callable<int(prepared, descriptor)>;
    auto descriptor_overloads = portal::create_export_overload_binder<portal::type_list<DescriptorContract, DescriptorContract>>(c,
        [](prepared, prepared&) noexcept { ++natives; return 9; });
    args = {V::boolean(true), V::signed_integer(1)};
    CHECK(!descriptor_overloads.dispatch_optional(2, codec::consuming_vector_accessor{&args}));
    CHECK(descriptor_calls == 2 && conversions == 0 && natives == 0);
    CHECK(created == 2 && destroyed == 2 && alive == 0);
    scalar_calls = 0;
    auto custom = portal::create_export_callable_binder<int(custom_scalar)>(c, [](int value) noexcept { return value; });
    CHECK(custom(V::signed_integer(27)).as_signed() == 27 && scalar_calls == 1);
    CHECK(!custom.call_optional(V::boolean(true)) && scalar_calls == 1);

    reset();
    auto aliases = portal::create_export_overload_binder<AliasList>(c, alias_terminal{});
    V shared = V::string("payload");
    CHECK(aliases(shared, shared).as_signed() == 1);
    CHECK(shared.copy_string() == "payload" && conversions == 2 && natives == 1 && fallbacks == 0);
    CHECK(created == destroyed && alive == 0);

    reset();
    transport wire{V::string("payload")};
    portal::rpc_backend::import_context_t<transport> imported(wire, 1);
    { auto p = portal::rpc_backend::invoke<prepared>(imported, portal::no_receiver_t{}); CHECK(*p.text == "payload"); }
    CHECK(probes == 0 && conversions == 1 && created == destroyed);
    portal::rpc::router router;
    router.define("legacy", legacy_binder{});
    router.define("legacy_group", legacy_overload_binder{});
    V output; std::string error;
    CHECK(codec::try_decode_response(router.dispatch(codec::encode_request_values(codec::symbol_id("legacy"), V::string("abc"))), output, error) == codec::response_status::ok);
    CHECK(output.as_signed() == 3);
    CHECK(codec::try_decode_response(router.dispatch(codec::encode_request_values(codec::symbol_id("legacy_group"), V::string("abcd"))), output, error) == codec::response_status::ok);
    CHECK(output.as_signed() == 4);
    reset();
    router.define("prepared", rvalue);
    CHECK(codec::try_decode_response(router.dispatch(codec::encode_request_values(codec::symbol_id("prepared"), V::string("payload"))), output, error) == codec::response_status::ok);
    CHECK(output.as_signed() == 7 && probes == 1 && conversions == 1 && natives == 1 && created == destroyed);
    reset();
    CHECK(codec::try_decode_response(router.dispatch(codec::encode_request_values(codec::symbol_id("prepared"), V::boolean(true))), output, error) == codec::response_status::remote_error);
    CHECK(conversions == 0 && natives == 0);

#if PORTAL_HAS_EXCEPTIONS
    // std::ref explicitly requests aliasing and is unwrapped by std::make_tuple.
    // Unlike plain lvalues, these are not stable independent dynamic inputs:
    // consuming one invalidates the other's earlier nonempty probe. No fallback.
    reset();
    shared = V::string("payload");
    bool alias_failed = false;
    try { (void)aliases(std::ref(shared), std::ref(shared)); }
    catch (const portal::bad_conversion& e) {
        alias_failed = true;
        CHECK(std::strcmp(e.what(), "portal export argument rejected an accepted probe") == 0);
    }
    CHECK(alias_failed && probes == 2 && conversions == 2 && natives == 0 && fallbacks == 0);
    CHECK(shared.string_size() == 0 && created == destroyed && alive == 0);
    reset();
    bool mismatch = false;
    try { (void)rvalue(V::boolean(true)); }
    catch (const portal::bad_conversion& e) { mismatch = true; CHECK(std::strcmp(e.what(), "portal export argument conversion failed") == 0); }
    CHECK(mismatch && conversions == 0 && natives == 0);
    mismatch = false;
    try { (void)group(V::string("payload"), V::boolean(true)); }
    catch (const portal::bad_conversion& e) { mismatch = true; CHECK(std::strcmp(e.what(), "portal export overload has no matching signature") == 0); }
    CHECK(mismatch && conversions == 0 && natives == 0);
    // Conversion failure after another argument committed cleans up without
    // invoking the native function or trying a later overload. No rollback is promised.
    reset(); mode = 4;
    args = {V::string("payload"), V::string("payload")};
    bool late_failure = false;
    try { (void)aliases.dispatch_optional(2, codec::consuming_vector_accessor{&args}); }
    catch (const std::runtime_error& e) { late_failure = std::strcmp(e.what(), "late converter failure") == 0; }
    CHECK(late_failure && probes == 2 && conversions == 2 && natives == 0 && fallbacks == 0);
    CHECK(created == 1 && destroyed == 1 && alive == 0);
    for (int failure = 1; failure <= 3; ++failure) {
        reset(); mode = failure;
        auto stop = portal::create_export_overload_binder<TerminalList>(c, terminal{});
        args = {V::string("payload")};
        bool threw = false;
        try { (void)stop.dispatch_optional(1, codec::consuming_vector_accessor{&args}); }
        catch (const std::exception& e) {
            threw = true;
            CHECK(std::strcmp(e.what(), failure == 1 ? "portal export argument rejected an accepted probe" : failure == 2 ? "converter failure" : "native failure") == 0);
        }
        CHECK(threw && probes == 1 && conversions == 1 && fallbacks == 0);
        CHECK(natives == (failure == 3 ? 1 : 0) && alive == 0 && created == destroyed);
    }
#elif defined(__unix__)
    // A contract violation must abort, rather than return mismatch and try the fallback.
    for (int alias_case = 0; alias_case != 2; ++alias_case) {
    pid_t pid = fork(); CHECK(pid >= 0);
    if (pid == 0) {
        if (alias_case) {
            mode = 0;
            shared = V::string("payload");
            (void)aliases(std::ref(shared), std::ref(shared));
            _exit(92);
        }
        mode = 1;
        auto stop = portal::create_export_overload_binder<TerminalList>(c, terminal{});
        args = {V::string("payload")};
        (void)stop.dispatch_optional(1, codec::consuming_vector_accessor{&args});
        _exit(91);
    }
    int status; CHECK(waitpid(pid, &status, 0) == pid);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    }
#endif
    std::puts("RPC-only probe contract tests passed");
}
