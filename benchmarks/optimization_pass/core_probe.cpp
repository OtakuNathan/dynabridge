// Build this same source against both checkouts with identical C++14 flags.
// The optional argv[1] is the async iteration count; payload cases use 1/32 of it.
// argv[2] == --threaded-process enters the runtime's multithreaded mode first.
#include <portal/async.h>
#include <portal/export_callable.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

namespace {
std::atomic<std::uint64_t> new_calls{0};
std::uint64_t dynamic_copies = 0;
bool count_events = false;
}

void* operator new(std::size_t size) {
    if (count_events) new_calls.fetch_add(1, std::memory_order_relaxed);
    if (auto memory = std::malloc(size ? size : 1)) return memory;
    std::abort();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

namespace {
using clock_type = std::chrono::steady_clock;

template<class Run>
void measure(const char* scenario, std::size_t iterations, std::uint64_t expected, Run run) {
    for (std::size_t i = 0; i != std::min(iterations, std::size_t(2000)); ++i) {
        if (run() != expected) std::abort();
    }
    // Count allocations/copies in a separate untimed sample. Neither counter is
    // written in the timed loop, so removing copies cannot also remove measured
    // instrumentation writes and inflate the apparent payload speedup.
    const auto samples = std::min(iterations, std::size_t(100));
    const auto allocations_before = new_calls.load(std::memory_order_relaxed);
    const auto copies_before = dynamic_copies;
    count_events = true;
    for (std::size_t i = 0; i != samples; ++i) {
        if (run() != expected) std::abort();
    }
    count_events = false;
    const auto allocations = new_calls.load(std::memory_order_relaxed) - allocations_before;
    const auto copies = dynamic_copies - copies_before;
    std::uint64_t checksum = 0;
    const auto begin = clock_type::now();
    for (std::size_t i = 0; i != iterations; ++i) checksum += run();
    const auto elapsed = clock_type::now() - begin;
    if (checksum != expected * iterations) std::abort();
    std::printf("%s,%zu,%.3f,%.3f,%.3f,%llu\n", scenario, iterations,
        std::chrono::duration<double, std::nano>(elapsed).count() / iterations,
        double(allocations) / samples, double(copies) / samples,
        static_cast<unsigned long long>(checksum));
}

template<bool Erased> struct projection;
template<> struct projection<false> {
    template<class Call> static Call apply(Call call) { return call; }
};
template<> struct projection<true> {
    template<class Call> static auto apply(Call call) {
        return portal::erase_invocation(std::move(call));
    }
};

// Reusable backend completion storage. This intentionally retains the owning
// token, without adding a std::function allocation to each deferred operation.
class completion_slot {
    typename std::aligned_storage<64, alignof(std::max_align_t)>::type storage_;
    void (*finish_)(void*) = nullptr;
    void (*destroy_)(void*) = nullptr;
public:
    ~completion_slot() { clear(); }
    template<class Token> void set(Token token) noexcept {
        static_assert(sizeof(Token) <= sizeof(storage_), "completion token storage");
        static_assert(alignof(Token) <= alignof(decltype(storage_)), "completion token alignment");
        if (destroy_) std::abort();
        new (&storage_) Token(std::move(token));
        finish_ = [](void* memory) {
            static_cast<Token*>(memory)->finish(portal::async_result<int>(42));
        };
        destroy_ = [](void* memory) { static_cast<Token*>(memory)->~Token(); };
    }
    void finish() {
        if (!finish_) std::abort();
        finish_(&storage_);
        clear();
    }
    void clear() noexcept {
        if (destroy_) {
            auto destroy = destroy_;
            destroy_ = nullptr;
            finish_ = nullptr;
            destroy(&storage_);
        }
    }
};

struct inline_operation {
    template<class Token> portal::async_submission operator()(int& value, Token done) const noexcept {
        done.finish(portal::async_result<int>(value + 1));
        return portal::async_submission::accept();
    }
};
struct deferred_operation {
    completion_slot* slot;
    template<class Token> portal::async_submission operator()(int&, Token done) const noexcept {
        slot->set(std::move(done));
        return portal::async_submission::accept();
    }
};

template<bool Erased>
void async_cases(std::size_t iterations) {
    auto context = std::make_shared<int>(41);
    measure(Erased ? "erased_inline" : "static_inline", iterations, 42, [&] {
        unsigned result = 0;
        auto call = projection<Erased>::apply(portal::make_invocation<int>(context, inline_operation{}));
        std::move(call).start([&](portal::async_result<int> value) noexcept {
            if (!value.has_value()) std::abort();
            result = static_cast<unsigned>(*value.value);
        });
        return result;
    });
    completion_slot pending;
    measure(Erased ? "erased_deferred" : "static_deferred", iterations, 42, [&] {
        unsigned result = 0;
        auto call = projection<Erased>::apply(portal::make_invocation<int>(context, deferred_operation{&pending}));
        std::move(call).start([&](portal::async_result<int> value) noexcept {
            if (!value.has_value()) std::abort();
            result = static_cast<unsigned>(*value.value);
        });
        pending.finish();
        return result;
    });
}

struct counted_dynamic {
    std::string text;
    explicit counted_dynamic(std::string value) : text(std::move(value)) {}
    counted_dynamic(const counted_dynamic& other) : text(other.text) {
        if (count_events) ++dynamic_copies;
    }
    counted_dynamic(counted_dynamic&&) noexcept = default;
};
struct payload_backend {
    using dynamic_value_t = counted_dynamic;
    using export_probe_policy = portal::rpc_export_probe_policy;
    struct context { using backend_t = payload_backend; };
    template<class T> struct converter;
};
template<> struct payload_backend::converter<std::string> {
    static bool probe(payload_backend::context&, const counted_dynamic&) noexcept { return true; }
    static portal::optional<std::string> from(payload_backend::context&, counted_dynamic& value) {
        return portal::optional<std::string>(std::move(value.text));
    }
    static portal::optional<std::string> from(payload_backend::context&, const counted_dynamic& value) {
        return portal::optional<std::string>(value.text);
    }
    static portal::optional<std::string> from(payload_backend::context&, counted_dynamic&& value) {
        return portal::optional<std::string>(std::move(value.text));
    }
    static counted_dynamic to(payload_backend::context&, std::string&& value) {
        return counted_dynamic(std::move(value));
    }
};

void payload_cases(std::size_t iterations) {
    payload_backend::context context;
    using overloads = portal::type_list<portal::free_callable<std::string(std::string)>>;
    auto binder = portal::create_export_overload_binder<overloads>(context,
        [](std::string value) { return value; });
    const counted_dynamic input(std::string(65536, 'p'));
    measure("direct_overload_64k", iterations, input.text.size(), [&] {
        auto result = binder(input);
        if (result.text != input.text) std::abort();
        return result.text.size();
    });
    measure("optional_overload_64k", iterations, input.text.size(), [&] {
        auto result = binder.call_optional(input);
        if (!result || result->text != input.text) std::abort();
        return result->text.size();
    });
    if (input.text != std::string(65536, 'p')) std::abort();
}
}

int main(int argc, char** argv) {
    const auto iterations = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1000000ull;
    if (iterations == 0) return 2;
    if (argc > 2 && std::string(argv[2]) == "--threaded-process") {
        // Exercise shared_ptr accounting after libstdc++/glibc has observed a
        // second thread. Timed calls still run on this thread; this is not a
        // thread handoff measurement, and thread startup is outside warmup.
        std::thread entered_multithreaded_mode([] {});
        entered_multithreaded_mode.join();
    }
    std::puts("scenario,iterations,ns_per_call,new_calls_per_call,dynamic_copies_per_call,checksum");
    async_cases<false>(static_cast<std::size_t>(iterations));
    async_cases<true>(static_cast<std::size_t>(iterations));
    payload_cases(std::max(std::size_t(256), static_cast<std::size_t>(iterations / 32)));
}
