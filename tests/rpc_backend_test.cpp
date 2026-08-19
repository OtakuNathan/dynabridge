#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#define DYNABRIDGE_IMPORT_DEF "tests/rpc_import.def"
#define DYNABRIDGE_EXPORT_DEF "tests/rpc_export.def"
#include "dynabridge/bridge.h"
#undef DYNABRIDGE_EXPORT_DEF
#undef DYNABRIDGE_IMPORT_DEF

#include "dynabridge/backends/rpc.h"

namespace {
    int add(int left, unsigned right) {
        return left + static_cast<int>(right);
    }

    std::string echo(std::string text) {
        return "rpc:" + text;
    }

    struct notification_sink {
        int* value;

        void operator()(int next) const {
            *value = next;
        }
    };

    struct unknown_symbol {
        static const char* symbol_name() noexcept { return "missing"; }
    };
}

int main() {
    dynabridge::rpc_backend::export_context_t export_ctx;
    dynabridge::rpc::router module;

    dynabridge::export_rpc_add(export_ctx, module, &add);
    dynabridge::export_rpc_echo(export_ctx, module, &echo);
    dynabridge::export_rpc_move(export_ctx, module, &echo);

    int notified = 0;
    dynabridge::export_rpc_notify<void(int)>(
        export_ctx, module, notification_sink{&notified});
    dynabridge::export_rpc_scale<double(double)>(
        export_ctx, module, [](double value) { return value * 1.5; });
    dynabridge::export_rpc_not<bool(bool)>(
        export_ctx, module, [](bool value) { return !value; });

    auto select = dynabridge::export_rpc_select(export_ctx, module)
        .bind<int(int)>([](int value) { return value * 10; })
        .bind<int(unsigned)>([](unsigned value) { return static_cast<int>(value) + 1000; });
    select.commit();

    if (module.size() != 7) {
        return 1;
    }

    dynabridge::rpc::loopback_transport transport(module);
    using context_t = dynabridge::rpc_backend::import_context_t<
        dynabridge::rpc::loopback_transport>;

    auto add_ctx = dynabridge::import_from<dynabridge::import_symbols::rpc_add, context_t>(transport);
    if (dynabridge::call_rpc_add(add_ctx, -2, 9u) != 7) {
        return 2;
    }

    auto echo_ctx = dynabridge::import_from<dynabridge::import_symbols::rpc_echo, context_t>(transport);
    if (dynabridge::call_rpc_echo(echo_ctx, std::string("hello")) != "rpc:hello"
            || dynabridge::call_rpc_echo(echo_ctx, std::string()) != "rpc:") {
        return 3;
    }

    auto move_ctx = dynabridge::import_from<dynabridge::import_symbols::rpc_move, context_t>(transport);
    std::string movable = "move";
    if (dynabridge::call_rpc_move(move_ctx, std::move(movable)) != "rpc:move") {
        return 10;
    }

    auto notify_ctx = dynabridge::import_from<dynabridge::import_symbols::rpc_notify, context_t>(transport);
    dynabridge::call_rpc_notify(notify_ctx, 42);
    if (notified != 42) {
        return 4;
    }

    auto scale_ctx = dynabridge::import_from<dynabridge::import_symbols::rpc_scale, context_t>(transport);
    auto not_ctx = dynabridge::import_from<dynabridge::import_symbols::rpc_not, context_t>(transport);
    if (dynabridge::call_rpc_scale(scale_ctx, 2.0) != 3.0
            || dynabridge::call_rpc_not(not_ctx, true)) {
        return 9;
    }

    auto select_ctx = dynabridge::import_from<dynabridge::import_symbols::rpc_select, context_t>(transport);
    if (dynabridge::call_rpc_select(select_ctx, -3) != -30
            || dynabridge::call_rpc_select(select_ctx, 5u) != 1005) {
        return 5;
    }

    bool rejected = false;
    try {
        std::vector<dynabridge::rpc::value> args{
            dynabridge::rpc::value::boolean(true)
        };
        auto response = transport.round_trip(dynabridge::rpc::detail::encode_request(
            dynabridge::rpc::detail::symbol_id("rpc_select"), args));
        (void)dynabridge::rpc::detail::decode_response(response);
    } catch (const dynabridge::rpc::remote_error&) {
        rejected = true;
    }
    if (!rejected) {
        return 6;
    }

    bool missing = false;
    try {
        auto missing_ctx = dynabridge::import_from<unknown_symbol, context_t>(transport);
        (void)context_t::backend_t::template invoke<int>(
            missing_ctx, dynabridge::no_receiver_t{}, 1);
    } catch (const dynabridge::rpc::remote_error&) {
        missing = true;
    }
    if (!missing) {
        return 7;
    }

    bool malformed = false;
    try {
        auto response = transport.round_trip(dynabridge::rpc::bytes{1, 2, 3});
        (void)dynabridge::rpc::detail::decode_response(response);
    } catch (const dynabridge::rpc::remote_error&) {
        malformed = true;
    }
    if (!malformed) {
        return 8;
    }

    return 0;
}
