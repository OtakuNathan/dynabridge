#include <portal/async.h>
#include <functional>
#include <memory>

int main() {
    std::function<void()> finish;
    auto call = portal::make_invocation<int>(std::make_shared<int>(7),
        [&](int& value, auto done) noexcept {
            finish = [done, value] { done.finish(portal::async_result<int>(value)); };
            return portal::async_submission::accept();
        });
    std::move(call).start([&](portal::async_result<int>) noexcept { finish = {}; });
    finish();
}
