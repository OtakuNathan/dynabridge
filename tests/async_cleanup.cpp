#include <portal/async.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <thread>

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "async cleanup failed at line %d: %s\n", __LINE__, #condition); \
    std::abort(); \
} } while (false)

namespace {
using portal::async_error;
using portal::async_result;
using portal::async_submission;

struct destructor_callback {
    std::function<void()> callback;
    explicit destructor_callback(std::function<void()> f) : callback(std::move(f)) {}
    ~destructor_callback() { callback(); }
};

void final_token_destruction() {
    std::function<void()> finish;
    auto owner = std::make_shared<int>(7);
    std::weak_ptr<int> lifetime = owner;
    unsigned deliveries = 0;
    auto call = portal::make_invocation<int>(std::move(owner),
        [&](int& value, auto done) noexcept {
            finish = [done, value] { done.finish(async_result<int>(value)); };
            return async_submission::accept();
        });
    std::move(call).start([&](async_result<int> result) noexcept {
        CHECK(result.has_value() && *result.value == 7);
        CHECK(!lifetime.expired());
        // Destroy the executing callback and the final externally owned token.
        finish = {};
        CHECK(!lifetime.expired());
        ++deliveries;
    });
    finish();
    CHECK(deliveries == 1 && lifetime.expired());
}

void detach_reentrant_retirement() {
    std::function<void()> finish, detach;
    auto owner = std::make_shared<int>(7);
    std::weak_ptr<int> lifetime = owner;
    unsigned deliveries = 0;
    auto call = portal::make_invocation<int>(std::move(owner),
        [&](int&, auto done) noexcept {
            finish = [done] { done.finish(async_result<int>(7)); };
            detach = [done] { done.detach(std::make_error_code(std::errc::timed_out)); };
            return async_submission::accept();
        });
    std::move(call).start([&](async_result<int> result) noexcept {
        CHECK(!result.has_value() && result.error.code == std::errc::timed_out);
        CHECK(!lifetime.expired());
        finish();
        CHECK(lifetime.expired());
        finish = {};
        detach = {};
        ++deliveries;
    });
    detach();
    CHECK(deliveries == 1 && lifetime.expired());
}

void reentrant_resource_destructors() {
    std::function<void()> finish, detach;
    unsigned contexts = 0, operations = 0, accesses = 0, deliveries = 0;
    auto owner = std::make_shared<destructor_callback>([&] {
        ++contexts;
        finish();
        detach();
    });
    auto captured = std::unique_ptr<destructor_callback>(new destructor_callback([&] {
        ++operations;
        finish();
        detach();
    }));
    auto access = std::make_shared<destructor_callback>([&] {
        ++accesses;
        finish();
        detach();
    });
    auto call = portal::make_invocation<void>(std::move(owner),
        [&, captured = std::move(captured)](destructor_callback&, auto done) noexcept {
            finish = [done] { done.finish(async_result<void>()); };
            detach = [done] { done.detach(std::make_error_code(std::errc::timed_out)); };
            return async_submission::accept();
        }, std::move(access));
    std::move(call).start([&](async_result<void> result) noexcept {
        CHECK(result.has_value() && accesses == 1);
        CHECK(contexts == 0 && operations == 0);
        ++deliveries;
    });
    finish();
    CHECK(contexts == 1 && operations == 1 && accesses == 1 && deliveries == 1);
    finish();
    detach();
    CHECK(contexts == 1 && operations == 1 && accesses == 1 && deliveries == 1);
}

struct sticky_handler {
    std::shared_ptr<int> marker;
    std::function<void()>* finish;
    unsigned* deliveries;
    sticky_handler(std::shared_ptr<int> m, std::function<void()>* f, unsigned* d) noexcept
        : marker(std::move(m)), finish(f), deliveries(d) {}
    // A conforming move is allowed to leave the source owning its captures.
    sticky_handler(sticky_handler&& other) noexcept
        : marker(other.marker), finish(other.finish), deliveries(other.deliveries) {}
    sticky_handler(const sticky_handler&) = delete;
    void operator()(async_result<int> result) noexcept {
        CHECK(result.has_value() && marker.use_count() == 1);
        *finish = {};
        ++*deliveries;
    }
};

void sticky_capture_final_token() {
    std::function<void()> finish;
    unsigned deliveries = 0;
    auto call = portal::make_invocation<int>(std::make_shared<int>(7),
        [&](int& value, auto done) noexcept {
            finish = [done, value] { done.finish(async_result<int>(value)); };
            return async_submission::accept();
        });
    std::move(call).start(sticky_handler(std::make_shared<int>(1), &finish, &deliveries));
    finish();
    CHECK(deliveries == 1);
}

void inline_submit_lifetime() {
    bool in_submit = false;
    unsigned contexts = 0, operations = 0, deliveries = 0;
    auto owner = std::make_shared<destructor_callback>([&] {
        CHECK(!in_submit);
        ++contexts;
    });
    auto captured = std::unique_ptr<destructor_callback>(new destructor_callback([&] {
        CHECK(!in_submit);
        ++operations;
    }));
    auto access = std::make_shared<int>(7);
    std::weak_ptr<int> access_lifetime = access;
    auto call = portal::make_invocation<void>(std::move(owner),
        [&, captured = std::move(captured)](destructor_callback&, auto done) noexcept {
            in_submit = true;
            done.finish(async_result<void>());
            CHECK(contexts == 0 && operations == 0);
            in_submit = false;
            return async_submission::accept();
        }, std::move(access));
    std::move(call).start([&](async_result<void> result) noexcept {
        CHECK(result.has_value() && access_lifetime.expired());
        CHECK(in_submit && contexts == 0 && operations == 0);
        ++deliveries;
    });
    CHECK(contexts == 1 && operations == 1 && deliveries == 1);
}

void concurrent_submit_finish_detach() {
    for (unsigned repeat = 0; repeat != 200; ++repeat) {
        std::atomic<bool> release{false}, in_submit{false};
        std::atomic<unsigned> contexts{0}, operations{0}, accesses{0}, deliveries{0};
        std::thread finishing, detaching;
        auto owner = std::make_shared<destructor_callback>([&] {
            CHECK(!in_submit.load());
            ++contexts;
        });
        auto captured = std::unique_ptr<destructor_callback>(new destructor_callback([&] {
            CHECK(!in_submit.load());
            ++operations;
        }));
        auto access = std::make_shared<destructor_callback>([&] { ++accesses; });
        auto call = portal::make_invocation<void>(std::move(owner),
            [&, captured = std::move(captured)](destructor_callback&, auto done) noexcept {
                in_submit = true;
                finishing = std::thread([&, done] {
                    while (!release.load()) std::this_thread::yield();
                    done.finish(async_result<void>());
                    done.finish(async_result<void>());
                });
                detaching = std::thread([&, done] {
                    while (!release.load()) std::this_thread::yield();
                    done.detach(std::make_error_code(std::errc::timed_out));
                });
                release = true;
                if ((repeat & 1u) == 0) std::this_thread::yield();
                CHECK(contexts == 0 && operations == 0);
                in_submit = false;
                return async_submission::accept();
            }, std::move(access));
        std::move(call).start([&](async_result<void> result) noexcept {
            if (result.has_value()) CHECK(accesses == 1);
            else CHECK(result.error.code == std::errc::timed_out);
            ++deliveries;
        });
        finishing.join();
        detaching.join();
        CHECK(contexts == 1 && operations == 1 && accesses == 1 && deliveries == 1);
    }
}
}

int main() {
    final_token_destruction();
    detach_reentrant_retirement();
    reentrant_resource_destructors();
    sticky_capture_final_token();
    inline_submit_lifetime();
    concurrent_submit_finish_detach();
    std::puts("async cleanup contracts passed");
}
