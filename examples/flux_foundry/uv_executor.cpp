#define PORTAL_IMPORT_DEF "examples/flux_foundry/import.def"
#define PORTAL_EXPORT_DEF "examples/flux_foundry/import.def"
#include "portal/portal.h"
#include "portal/backends/rpc.h"
#include <executor/uv_executor.h>

#include <flow/flow.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <thread>

namespace {
    namespace ff = flux_foundry;
    using result_t = ff::result_t<int, std::exception_ptr>;

    int executor_add(int left, unsigned right) {
        return left + static_cast<int>(right);
    }

    struct result_state {
        explicit result_state(uv_loop_t* target) : loop(target) {
        }

        uv_loop_t* loop;
        std::mutex mutex;
        std::condition_variable ready;
        bool done = false;
        int value = 0;
        std::exception_ptr error;
    };

    struct result_receiver {
        using value_type = result_t;
        result_state* state;

        void emplace(result_t&& result) noexcept {
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (result.has_value()) {
                    state->value = result.value();
                } else {
                    state->error = std::move(result).error();
                }
                state->done = true;
            }
            state->ready.notify_one();
            uv_stop(state->loop);
        }
    };
}

int main(int argc, char** argv) {
    const std::size_t capacity = argc > 1
        ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10))
        : ff::uv_executor::default_capacity;
    if (!capacity) return 1;
    uv_loop_t loop;
    if (uv_loop_init(&loop) != 0) {
        return 1;
    }

    int status = 0;
    {
        ff::uv_executor executor(&loop, capacity);
        portal::rpc_backend::export_context_t export_ctx;
        portal::rpc::router module;
        portal::export_executor_add(export_ctx, module, &executor_add);
        portal::rpc::loopback_transport transport(module);
        using context_t = portal::rpc_backend::import_context_t<
            portal::rpc::loopback_transport>;
        auto ctx = portal::import_from<
            portal::import_symbols::executor_add, context_t>(transport);

        result_state result(&loop);
        auto blueprint = ff::make_blueprint<int>()
            | ff::via(&executor)
            | ff::then([&ctx](result_t&& input) -> result_t {
                return result_t(ff::value_tag,
                    portal::call_executor_add(
                        ctx, std::move(input).value(), 2u));
            })
            | ff::end();
        auto runner = ff::make_fast_runner(
            std::move(blueprint), result_receiver{&result});

        std::thread producer([&runner] { runner(40); });
        uv_run(&loop, UV_RUN_DEFAULT);
        producer.join();
        if (result.error || result.value != 42) {
            status = 2;
        }

        constexpr std::size_t iterations = 50000;
        std::atomic<std::size_t> remaining{iterations};
        const auto start = std::chrono::steady_clock::now();
        std::thread benchmark_producer([&] {
            for (std::size_t index = 0; index < iterations; ++index) {
                ff::task_wrapper_sbo task(
                    [&remaining, &loop]() noexcept {
                        if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                            uv_stop(&loop);
                        }
                    });
                // This producer is separate from the loop and close follows
                // its join. A full queue leaves task intact for retry.
                while (!executor.try_dispatch(std::move(task))) {
                    std::this_thread::yield();
                }
            }
        });
        uv_run(&loop, UV_RUN_DEFAULT);
        benchmark_producer.join();
        const auto elapsed = std::chrono::steady_clock::now() - start;

        if (remaining.load(std::memory_order_acquire) != 0) {
            status = 3;
        } else if (status == 0) {
            const double ns_per_task =
                std::chrono::duration<double, std::nano>(elapsed).count()
                / static_cast<double>(iterations);
            std::printf("uv executor: portal + FF result=%d, %.1f ns/task\n",
                result.value, ns_per_task);
        }

        executor.close();
        uv_run(&loop, UV_RUN_DEFAULT);
        if (!executor.closed()) status = 4;
    }

    if (uv_loop_close(&loop) != 0) {
        return 4;
    }
    return status;
}
