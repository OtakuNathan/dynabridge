#include <algorithm>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <utility>
#include <vector>

#include "flow/flow.h"

#if defined(_MSC_VER)
#define DYNABRIDGE_BENCH_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define DYNABRIDGE_BENCH_FORCE_INLINE inline __attribute__((always_inline))
#else
#define DYNABRIDGE_BENCH_FORCE_INLINE inline
#endif

namespace {
    namespace ff = flux_foundry;

    using value_t = std::uint32_t;
    using error_t = std::exception_ptr;
    using result_t = ff::result_t<value_t, error_t>;

    DYNABRIDGE_BENCH_FORCE_INLINE value_t step(value_t value) noexcept {
        return (value ^ 0x5a5a5a5au) + (value >> 3u);
    }

    DYNABRIDGE_BENCH_FORCE_INLINE value_t direct_pipeline(value_t value) noexcept {
        value = step(value); value = step(value); value = step(value); value = step(value);
        value = step(value); value = step(value); value = step(value); value = step(value);
        value = step(value); value = step(value); value = step(value); value = step(value);
        value = step(value); value = step(value); value = step(value); value = step(value);
        value = step(value); value = step(value); value = step(value); value = step(value);
        return value;
    }

    auto make_fast_pipeline() {
        return ff::make_blueprint<value_t>()
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::end();
    }

    struct immediate_resume_awaitable final
        : ff::fast_awaitable_base<immediate_resume_awaitable, value_t, error_t> {
        using async_result_type = result_t;

        result_t result;

        explicit immediate_resume_awaitable(result_t&& input) noexcept
            : result(std::move(input)) {
        }

        int submit() noexcept {
            this->resume(std::move(result));
            return 0;
        }

        bool available() const noexcept { return true; }
        void cancel() noexcept {}
    };

    auto make_sync_resume_pipeline() {
        return ff::make_blueprint<value_t>()
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::await<immediate_resume_awaitable>()
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::transform([](value_t value) noexcept { return step(value); })
            | ff::end();
    }

    struct sink_receiver {
        using value_type = result_t;

        volatile std::uint64_t* sink;

        void emplace(result_t&& result) noexcept {
            *sink += result.has_value() ? result.value() : 0u;
        }
    };

    struct coroutine_state {
        value_t input = 0;
        value_t output = 0;
    };

    struct ready_value {
        value_t value;

        bool await_ready() const noexcept { return true; }
        void await_suspend(std::coroutine_handle<>) const noexcept {}
        value_t await_resume() const noexcept { return value; }
    };

    class reusable_coroutine {
    public:
        struct promise_type;
        using handle_t = std::coroutine_handle<promise_type>;

        struct promise_type {
            reusable_coroutine get_return_object() noexcept {
                return reusable_coroutine(handle_t::from_promise(*this));
            }

            std::suspend_never initial_suspend() const noexcept { return {}; }
            std::suspend_always final_suspend() const noexcept { return {}; }
            void return_void() const noexcept {}
            void unhandled_exception() const noexcept { std::terminate(); }
        };

        explicit reusable_coroutine(handle_t handle) noexcept
            : handle_(handle) {
        }

        reusable_coroutine(const reusable_coroutine&) = delete;
        reusable_coroutine& operator=(const reusable_coroutine&) = delete;

        reusable_coroutine(reusable_coroutine&& other) noexcept
            : handle_(std::exchange(other.handle_, {})) {
        }

        ~reusable_coroutine() {
            if (handle_) {
                handle_.destroy();
            }
        }

        void resume() noexcept { handle_.resume(); }

    private:
        handle_t handle_;
    };

    reusable_coroutine make_coroutine_pipeline(coroutine_state* state) {
        for (;;) {
            co_await std::suspend_always{};
            auto value = state->input;
            value = co_await ready_value{step(value)};
            value = co_await ready_value{step(value)};
            value = co_await ready_value{step(value)};
            value = co_await ready_value{step(value)};
            value = co_await ready_value{step(value)};
            value = co_await ready_value{step(value)};
            value = co_await ready_value{step(value)};
            value = co_await ready_value{step(value)};
            value = co_await ready_value{step(value)};
            value = co_await ready_value{step(value)};
            value = co_await ready_value{step(value)};
            value = co_await ready_value{step(value)};
            value = co_await ready_value{step(value)};
            value = co_await ready_value{step(value)};
            value = co_await ready_value{step(value)};
            value = co_await ready_value{step(value)};
            value = co_await ready_value{step(value)};
            value = co_await ready_value{step(value)};
            value = co_await ready_value{step(value)};
            value = co_await ready_value{step(value)};
            state->output = value;
        }
    }

    struct benchmark_result {
        const char* name;
        double median_ns;
        double max_ns;
    };

    template <typename Function>
    benchmark_result benchmark(
        const char* name,
        std::size_t warmup,
        std::size_t iterations,
        Function&& function)
    {
        for (std::size_t i = 0; i < warmup; ++i) {
            function(static_cast<value_t>(i));
        }

        std::vector<double> samples;
        samples.reserve(7);
        for (int round = 0; round < 7; ++round) {
            const auto start = std::chrono::steady_clock::now();
            for (std::size_t i = 0; i < iterations; ++i) {
                function(static_cast<value_t>(i));
            }
            const auto elapsed = std::chrono::steady_clock::now() - start;
            samples.push_back(
                std::chrono::duration<double, std::nano>(elapsed).count()
                / static_cast<double>(iterations));
        }
        std::sort(samples.begin(), samples.end());
        return benchmark_result{name, samples[3], samples[6]};
    }

    void print(const benchmark_result& result) {
        std::printf("%-34s median=%8.2f ns/op  max=%8.2f ns/op\n",
            result.name, result.median_ns, result.max_ns);
    }
}

int main() {
    constexpr std::size_t warmup = 20000;
    constexpr std::size_t iterations = 3000000;
    volatile std::uint64_t sink = 0;

    auto blueprint = make_fast_pipeline();
    auto fast_runner = ff::make_fast_runner(
        std::move(blueprint), sink_receiver{&sink});
    auto sync_resume_blueprint = make_sync_resume_pipeline();
    auto sync_resume_runner = ff::make_fast_runner(
        std::move(sync_resume_blueprint), sink_receiver{&sink});
    auto sync_resume_view_blueprint = make_sync_resume_pipeline();
    auto sync_resume_view_runner = ff::make_fast_runner_view(
        sync_resume_view_blueprint, sink_receiver{&sink});

    coroutine_state state;
    auto coroutine = make_coroutine_pipeline(&state);

    print(benchmark("direct C++: 20 steps", warmup, iterations,
        [&sink](value_t value) {
            sink += direct_pipeline(value);
        }));
    print(benchmark("FF fast_runner: 20 sync nodes", warmup, iterations,
        [&fast_runner](value_t value) {
            fast_runner(value);
        }));
    print(benchmark("FF owning runner: sync resume", warmup, iterations,
        [&sync_resume_runner](value_t value) {
            sync_resume_runner(value);
        }));
    print(benchmark("FF runner view: sync resume", warmup, iterations,
        [&sync_resume_view_runner](value_t value) {
            sync_resume_view_runner(value);
        }));
    print(benchmark("C++20 coroutine: reused frame", warmup, iterations,
        [&state, &coroutine, &sink](value_t value) {
            state.input = value;
            coroutine.resume();
            sink += state.output;
        }));

    std::printf("sink=%llu\n", static_cast<unsigned long long>(sink));
    return 0;
}
