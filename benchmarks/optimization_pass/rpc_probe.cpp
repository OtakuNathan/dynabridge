// Build this same source with -std=c++14 -O3 -DNDEBUG -I<checkout> for each tree.
// Usage: rpc_probe [iterations] or rpc_probe --allocations-only.
#include "portal/portal.h"
#include "portal/backends/rpc.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <utility>

namespace {
bool count_allocations = false;
std::uint64_t allocation_count = 0;
std::uint64_t allocated_bytes = 0;

void* allocate(std::size_t size) {
    void* result = std::malloc(size == 0 ? 1 : size);
    if (result == nullptr) throw std::bad_alloc();
    if (count_allocations) {
        ++allocation_count;
        allocated_bytes += size;
    }
    return result;
}
}

void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {
using backend = portal::rpc_backend;
using import_context = backend::import_context_t<portal::rpc::loopback_transport>;

void check(bool result) {
    if (!result) {
        std::fputs("RPC probe verification failed\n", stderr);
        std::abort();
    }
}

template <typename Sequence>
struct scalar_case;

template <std::size_t... Indices>
struct scalar_case<std::index_sequence<Indices...>> {
    using signature = int(decltype(static_cast<void>(Indices), int{})...);

    static int sum(decltype(static_cast<void>(Indices), int{})... values) noexcept {
        int result = 0;
        using swallow = int[];
        (void)swallow{0, (result += values, 0)...};
        return result;
    }

    static int invoke(import_context& context, int seed) {
        return backend::invoke<int>(context, portal::no_receiver_t{}, (seed + static_cast<int>(Indices))...);
    }
};

template <typename Function>
void measure(const char* name, std::size_t iterations, bool timings, Function function) {
    std::uint64_t warmup_checksum = 0;
    for (unsigned i = 0; i < 2048; ++i) warmup_checksum += function(i & 255u);

    constexpr std::size_t allocation_iterations = 100;
    allocation_count = 0;
    allocated_bytes = 0;
    count_allocations = true;
    std::uint64_t allocation_checksum = 0;
    for (std::size_t i = 0; i < allocation_iterations; ++i) allocation_checksum += function(i & 255u);
    count_allocations = false;
    const auto allocations = allocation_count;
    const auto bytes = allocated_bytes;

    std::uint64_t checksum = 0;
    double nanoseconds = 0;
    if (timings) {
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < iterations; ++i) checksum += function(i & 255u);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        nanoseconds = std::chrono::duration<double, std::nano>(elapsed).count() / iterations;
    }
    // All checksums are emitted so even the allocation-only run has observable
    // results. Allocator accounting is disabled throughout the timed region.
    std::printf("%s,%zu,%.3f,%.2f,%.2f,%llu,%llu,%llu\n", name, timings ? iterations : 0,
        nanoseconds, static_cast<double>(allocations) / allocation_iterations,
        static_cast<double>(bytes) / allocation_iterations,
        static_cast<unsigned long long>(checksum),
        static_cast<unsigned long long>(allocation_checksum),
        static_cast<unsigned long long>(warmup_checksum));
}

template <std::size_t Count>
void measure_scalar(std::size_t iterations, bool timings) {
    using test = scalar_case<std::make_index_sequence<Count>>;
    backend::export_context_t export_context;
    portal::rpc::router router;
    router.define("sum", portal::create_export_callable_binder<typename test::signature>(export_context, &test::sum));
    portal::rpc::loopback_transport transport(router);
    import_context context(transport, portal::rpc::detail::symbol_id("sum"));
    for (int seed : {0, 1, 127, 255}) {
        check(test::invoke(context, seed) == static_cast<int>(Count * seed + Count * (Count - 1) / 2));
    }
    char name[32];
    std::snprintf(name, sizeof(name), "scalar_%zu", Count);
    measure(name, iterations, timings, [&context](unsigned seed) {
        return static_cast<std::uint64_t>(test::invoke(context, static_cast<int>(seed)));
    });
}

void measure_string(std::size_t size, std::size_t iterations, bool timings) {
    backend::export_context_t export_context;
    portal::rpc::router router;
    router.define("echo", portal::create_export_callable_binder<std::string(std::string)>(export_context,
        [](std::string input) noexcept { return input; }));
    portal::rpc::loopback_transport transport(router);
    import_context context(transport, portal::rpc::detail::symbol_id("echo"));
    std::string payload(size, 'x');
    payload.front() = 'a';
    payload[size / 2] = '\0';
    payload.back() = 'z';
    check(backend::invoke<std::string>(context, portal::no_receiver_t{}, payload) == payload);
    char name[32];
    std::snprintf(name, sizeof(name), "string_%zu", size);
    measure(name, iterations, timings, [&context, &payload](unsigned) {
        auto result = backend::invoke<std::string>(context, portal::no_receiver_t{}, payload);
        return static_cast<std::uint64_t>(result.size()) + static_cast<unsigned char>(result.front())
            + static_cast<unsigned char>(result[result.size() / 2]) + static_cast<unsigned char>(result.back());
    });
}
}

int main(int argc, char** argv) {
    const bool timings = argc < 2 || std::strcmp(argv[1], "--allocations-only") != 0;
    const auto requested = timings && argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1000000ull;
    const std::size_t iterations = requested == 0 ? 1 : static_cast<std::size_t>(requested);
    std::puts("case,iterations,ns_per_call,allocations_per_call,allocated_bytes_per_call,checksum,allocation_checksum,warmup_checksum");
    measure_scalar<0>(iterations, timings);
    measure_scalar<1>(iterations, timings);
    measure_scalar<2>(iterations, timings);
    measure_scalar<4>(iterations, timings);
    measure_scalar<5>(iterations, timings);
    measure_scalar<8>(iterations, timings);
    measure_string(16, iterations, timings);
    measure_string(1024, iterations / 2 == 0 ? 1 : iterations / 2, timings);
    measure_string(65536, iterations / 32 == 0 ? 1 : iterations / 32, timings);
}
