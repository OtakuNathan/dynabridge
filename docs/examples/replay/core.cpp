#include "api.hpp"
#include <flow/flow.h>
#include <lease.hpp>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace arena {
namespace {
namespace ff = flux_foundry;
enum class error { invalid_price };
using result = ff::result_t<double, error>;

struct account {
    double cash;
    double units = 0;
    double pending = 0;
    double last_price = 0;
    unsigned observations = 0;
    unsigned fills = 0;
};

// The policy receives read authority, returns a value, and retains no aliases.
double decide_buy(lease::access::shared_access<account> input) noexcept {
    return input.read([](const account& current) noexcept {
        return std::min(100.0, current.cash);
    });
}

struct receiver {
    using value_type = result;
    bool* succeeded;
    void emplace(result&& output) noexcept { *succeeded = output.has_value(); }
};

bool number(const std::string& text, double& output) noexcept {
    if (text.empty()) return false;
    char* end = nullptr;
    errno = 0;
    output = std::strtod(text.c_str(), &end);
    return end == text.c_str() + text.size() && errno != ERANGE &&
           std::isfinite(output);
}

std::string failure(const char* message) noexcept {
    return std::string("{\"error\":\"") + message + "\"}";
}
}

std::string run_arena(const std::string& csv, const std::string& config) noexcept {
    double initial = 0;
    if (config.size() > 64 || !number(config, initial) ||
        initial <= 0 || initial > 1000000) {
        return failure("invalid_config");
    }
    if (csv.size() > 65536 || csv.compare(0, 6, "price\n") != 0) {
        return failure("invalid_csv");
    }

    account storage{initial}; // Stable address; outlives writer, readers, blueprint.
    auto coordinator = lease::access::make_rw(storage);
    auto blueprint = ff::make_blueprint<double, error>()
        | ff::then([&](result&& input) noexcept -> result {
            const double price = input.value();
            if (!std::isfinite(price) || price < 0.000001 || price > 1000000000) {
                return result(ff::error_tag, error::invalid_price);
            }
            // Fill only the preceding observation's order, then mark the account.
            coordinator.write([price](account& current) noexcept {
                if (current.pending > 0) {
                    current.cash -= current.pending;
                    current.units += current.pending / price;
                    ++current.fills;
                }
                current.pending = 0;
                current.last_price = price;
                ++current.observations;
            });
            return result(ff::value_tag, price);
        })
        | ff::transform([&](double price) noexcept {
            double budget = 0;
            {
                auto reader = coordinator.borrow_ro();
                budget = decide_buy(std::move(reader));
            } // All readers are gone before the writer is used again.
            coordinator.write([budget](account& current) noexcept {
                current.pending = budget;
            });
            return price;
        })
        | ff::end();

    unsigned rows = 0;
    std::size_t cursor = 6;
    while (cursor < csv.size()) {
        if (rows == 256) return failure("too_many_rows");
        const auto newline = csv.find('\n', cursor);
        const auto end = newline == std::string::npos ? csv.size() : newline;
        double price = 0;
        if (!number(csv.substr(cursor, end - cursor), price)) {
            return failure("invalid_csv");
        }
        bool succeeded = false;
        auto runner = ff::make_fast_runner_view(blueprint, receiver{&succeeded});
        runner(price);
        if (!succeeded) return failure("invalid_price");
        ++rows;
        cursor = end == csv.size() ? end : end + 1;
    }
    if (!rows) return failure("empty_csv");

    account snapshot{0};
    {
        auto reader = coordinator.borrow_ro();
        snapshot = reader.read([](const account& current) noexcept { return current; });
    }
    char output[512];
    const int size = std::snprintf(output, sizeof(output),
        "{\"cash\":%.9f,\"units\":%.9f,\"equity\":%.9f,"
        "\"pending_budget\":%.9f,\"observations\":%u,\"fills\":%u}",
        snapshot.cash, snapshot.units, snapshot.cash + snapshot.units * snapshot.last_price,
        snapshot.pending, snapshot.observations, snapshot.fills);
    if (size < 0 || static_cast<std::size_t>(size) >= sizeof(output)) {
        return failure("format_failed");
    }
    return std::string(output, static_cast<std::size_t>(size));
}
}
