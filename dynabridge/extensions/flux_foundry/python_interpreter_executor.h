#ifndef DYNABRIDGE_EXTENSIONS_FLUX_FOUNDRY_PYTHON_INTERPRETER_EXECUTOR_H
#define DYNABRIDGE_EXTENSIONS_FLUX_FOUNDRY_PYTHON_INTERPRETER_EXECUTOR_H

#include <Python.h>

#include <cassert>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include <task/task_wrapper.h>

namespace dynabridge {
    namespace flux_foundry_extensions {
        class python_interpreter_executor {
        public:
            using task_t = flux_foundry::task_wrapper_sbo;

            python_interpreter_executor() {
                if (!Py_IsInitialized()) {
                    throw std::runtime_error(
                        "python_interpreter_executor requires an initialized interpreter");
                }
                worker_ = std::thread(&python_interpreter_executor::run, this);
            }

            python_interpreter_executor(const python_interpreter_executor&) = delete;
            python_interpreter_executor& operator=(const python_interpreter_executor&) = delete;

            ~python_interpreter_executor() noexcept {
                shutdown();
            }

            void dispatch(task_t&& task) noexcept {
                assert(task && "cannot dispatch an empty FF task");
                if (!task) {
                    std::terminate();
                }

                bool run_inline = false;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (stopping_) {
                        assert(false && "cannot dispatch through a stopped Python executor");
                        std::terminate();
                    }
                    if (worker_id_ == std::this_thread::get_id()) {
                        run_inline = true;
                    } else {
                        try {
                            tasks_.emplace_back(std::move(task));
                        } catch (...) {
                            std::terminate();
                        }
                    }
                }

                if (run_inline) {
                    task();
                } else {
                    ready_.notify_one();
                }
            }

            void shutdown() noexcept {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (stopping_) {
                        return;
                    }
                    stopping_ = true;
                }
                ready_.notify_one();

                assert(worker_.get_id() != std::this_thread::get_id()
                    && "Python executor cannot join its own worker thread");
                PyThreadState* released = nullptr;
                if (worker_.joinable() && Py_IsInitialized() && PyGILState_Check()) {
                    released = PyEval_SaveThread();
                }
                if (worker_.joinable()) {
                    worker_.join();
                }
                if (released != nullptr && Py_IsInitialized()) {
                    PyEval_RestoreThread(released);
                }
            }

        private:
            void run() noexcept {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    worker_id_ = std::this_thread::get_id();
                }

                for (;;) {
                    std::deque<task_t> batch;
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        ready_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
                        batch.swap(tasks_);
                        if (batch.empty() && stopping_) {
                            break;
                        }
                    }

                    const PyGILState_STATE gil = PyGILState_Ensure();
                    for (auto& task : batch) {
                        task();
                    }
                    PyGILState_Release(gil);
                }
            }

            std::mutex mutex_;
            std::condition_variable ready_;
            std::deque<task_t> tasks_;
            std::thread worker_;
            std::thread::id worker_id_{};
            bool stopping_ = false;
        };
    }
}

#endif // DYNABRIDGE_EXTENSIONS_FLUX_FOUNDRY_PYTHON_INTERPRETER_EXECUTOR_H
