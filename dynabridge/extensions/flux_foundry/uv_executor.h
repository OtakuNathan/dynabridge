#ifndef DYNABRIDGE_EXTENSIONS_FLUX_FOUNDRY_UV_EXECUTOR_H
#define DYNABRIDGE_EXTENSIONS_FLUX_FOUNDRY_UV_EXECUTOR_H

#include <uv.h>

#include <cassert>
#include <cstddef>
#include <deque>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include <task/task_wrapper.h>

namespace dynabridge {
    namespace flux_foundry_extensions {
        class uv_executor {
        public:
            using task_t = flux_foundry::task_wrapper_sbo;
            static constexpr std::size_t max_tasks_per_round = 16;

            explicit uv_executor(uv_loop_t* loop)
                : state_(new state_t(loop)) {
            }

            uv_executor(const uv_executor&) = delete;
            uv_executor& operator=(const uv_executor&) = delete;

            ~uv_executor() noexcept {
                close();
            }

            void dispatch(task_t&& task) noexcept {
                assert(task && "cannot dispatch an empty FF task");
                state_t* const state = state_;
                if (state == nullptr || !task) {
                    std::terminate();
                }

                bool run_inline = false;
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    if (state->closing) {
                        assert(false && "cannot dispatch through a closing uv_executor");
                        std::terminate();
                    }
                    if (std::this_thread::get_id() == state->loop_thread
                            && state->tasks.empty()) {
                        run_inline = true;
                    } else {
                        try {
                            state->tasks.emplace_back(std::move(task));
                        } catch (...) {
                            std::terminate();
                        }
                        uv_async_send(&state->async);
                    }
                }

                if (run_inline) {
                    task();
                }
            }

            void close() noexcept {
                state_t* const state = state_;
                if (state == nullptr) {
                    return;
                }
                state_ = nullptr;

                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->closing = true;
                }
                uv_async_send(&state->async);
            }

            uv_loop_t* loop() const noexcept {
                return state_ == nullptr ? nullptr : state_->loop;
            }

        private:
            struct state_t {
                explicit state_t(uv_loop_t* target)
                    : loop(target), loop_thread(std::this_thread::get_id()) {
                    // Construction binds the executor to the current loop thread.
                    if (loop == nullptr || uv_async_init(loop, &async, &state_t::on_async) != 0) {
                        throw std::runtime_error("uv_async_init failed");
                    }
                    async.data = this;
                }

                static void on_async(uv_async_t* handle) noexcept {
                    state_t* const self = static_cast<state_t*>(handle->data);
                    std::deque<task_t> round;
                    bool rearm = false;
                    bool close = false;
                    {
                        std::lock_guard<std::mutex> lock(self->mutex);
                        for (std::size_t count = 0;
                                count < max_tasks_per_round && !self->tasks.empty();
                                ++count) {
                            round.emplace_back(std::move(self->tasks.front()));
                            self->tasks.pop_front();
                        }
                        rearm = !self->tasks.empty();
                        close = self->closing && !rearm;
                    }

                    for (auto& task : round) {
                        task();
                    }

                    if (rearm) {
                        uv_async_send(&self->async);
                    } else if (close) {
                        uv_close(reinterpret_cast<uv_handle_t*>(&self->async), &state_t::on_closed);
                    }
                }

                static void on_closed(uv_handle_t* handle) noexcept {
                    delete static_cast<state_t*>(handle->data);
                }

                uv_loop_t* loop;
                uv_async_t async{};
                std::thread::id loop_thread;
                std::mutex mutex;
                std::deque<task_t> tasks;
                bool closing = false;
            };

            state_t* state_;
        };
    }
}

#endif // DYNABRIDGE_EXTENSIONS_FLUX_FOUNDRY_UV_EXECUTOR_H
