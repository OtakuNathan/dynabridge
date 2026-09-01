#define DYNABRIDGE_IMPORT_DEF "examples/flux_foundry/import.def"
#include "dynabridge/bridge.h"
#include "dynabridge/backends/python.h"
#include "dynabridge/extensions/flux_foundry/python_interpreter_executor.h"

#include <flow/flow.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <exception>
#include <mutex>

namespace {
    namespace ff = flux_foundry;
    using result_t = ff::result_t<int, std::exception_ptr>;

    struct result_state {
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
        }
    };
}

int main() {
    Py_Initialize();
    int status = 0;
    {
        dynabridge::py_backend::object_ref globals(PyDict_New(),
            dynabridge::py_backend::ref_policy::owned);
        dynabridge::py_backend::object_ref script(globals ? PyRun_String(
                "def executor_add(a, b):\n"
                "    return a + b\n",
                Py_file_input, globals.get(), globals.get()) : nullptr,
            dynabridge::py_backend::ref_policy::owned);
        if (!globals || !script) {
            PyErr_Print();
            status = 1;
        } else {
            PyObject* callable = PyDict_GetItemString(globals.get(), "executor_add");
            dynabridge::py_backend::context_t ctx(callable);
            dynabridge::flux_foundry_extensions::python_interpreter_executor executor;
            result_state result;

            auto blueprint = ff::make_blueprint<int>()
                | ff::via(&executor)
                | ff::then([&ctx](result_t&& input) -> result_t {
                    return result_t(ff::value_tag,
                        dynabridge::call_executor_add(
                            ctx, std::move(input).value(), 2u));
                })
                | ff::end();
            auto runner = ff::make_fast_runner(
                std::move(blueprint), result_receiver{&result});

            PyThreadState* const released = PyEval_SaveThread();
            runner(40);
            {
                std::unique_lock<std::mutex> lock(result.mutex);
                result.ready.wait(lock, [&result] { return result.done; });
            }

            constexpr std::size_t iterations = 50000;
            std::atomic<std::size_t> remaining{iterations};
            std::mutex benchmark_mutex;
            std::condition_variable benchmark_ready;
            const auto start = std::chrono::steady_clock::now();
            for (std::size_t index = 0; index < iterations; ++index) {
                executor.dispatch(ff::task_wrapper_sbo(
                    [&remaining, &benchmark_ready]() noexcept {
                        if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                            benchmark_ready.notify_one();
                        }
                    }));
            }
            {
                std::unique_lock<std::mutex> lock(benchmark_mutex);
                benchmark_ready.wait(lock, [&remaining] {
                    return remaining.load(std::memory_order_acquire) == 0;
                });
            }
            const auto elapsed = std::chrono::steady_clock::now() - start;
            PyEval_RestoreThread(released);

            if (result.error || result.value != 42) {
                status = 2;
            } else {
                const double ns_per_task =
                    std::chrono::duration<double, std::nano>(elapsed).count()
                    / static_cast<double>(iterations);
                std::printf("Python executor: dynabridge + FF result=%d, %.1f ns/task\n",
                    result.value, ns_per_task);
            }
        }
    }
    Py_Finalize();
    return status;
}
