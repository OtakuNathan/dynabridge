#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#define PORTAL_IMPORT_DEF "tests/rpc_import.def"
#define PORTAL_EXPORT_DEF "tests/rpc_export.def"
#include "portal/portal.h"
#undef PORTAL_EXPORT_DEF
#undef PORTAL_IMPORT_DEF

#include "portal/backends/rpc.h"

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

    struct rpc_object_tag {
    };
}

static_assert(
    !portal::is_import_object_convertible<
        portal::rpc_backend,
        rpc_object_tag,
        portal::rpc_backend::export_context_t,
        portal::rpc::value>::value,
    "RPC stays scalar until its backend explicitly implements object identity");

static_assert(
    !portal::is_callable_importable<
        portal::rpc_backend,
        unknown_symbol,
        portal::rpc_backend::export_context_t,
        portal::rpc::value>::value,
    "RPC stays scalar until its backend explicitly implements callable identity");

int main() {
    portal::rpc_backend::export_context_t export_ctx;
    portal::rpc::router module;

    portal::export_rpc_add(export_ctx, module, &add);
    portal::export_rpc_echo(export_ctx, module, &echo);
    portal::export_rpc_move(export_ctx, module, &echo);

    int notified = 0;
    portal::export_rpc_notify<void(int)>(
        export_ctx, module, notification_sink{&notified});
    portal::export_rpc_scale<double(double)>(
        export_ctx, module, [](double value) { return value * 1.5; });
    portal::export_rpc_not<bool(bool)>(
        export_ctx, module, [](bool value) { return !value; });

    auto select = portal::export_rpc_select(export_ctx, module)
        .bind<int(int)>([](int value) { return value * 10; })
        .bind<int(unsigned)>([](unsigned value) { return static_cast<int>(value) + 1000; });
    select.commit();

    if (module.size() != 7) {
        return 1;
    }

    portal::rpc::loopback_transport transport(module);
    using context_t = portal::rpc_backend::import_context_t<
        portal::rpc::loopback_transport>;

    auto add_ctx = portal::import_from<portal::import_symbols::rpc_add, context_t>(transport);
    if (portal::call_rpc_add(add_ctx, -2, 9u) != 7) {
        return 2;
    }

    auto echo_ctx = portal::import_from<portal::import_symbols::rpc_echo, context_t>(transport);
    if (portal::call_rpc_echo(echo_ctx, std::string("hello")) != "rpc:hello"
            || portal::call_rpc_echo(echo_ctx, std::string()) != "rpc:") {
        return 3;
    }

    auto move_ctx = portal::import_from<portal::import_symbols::rpc_move, context_t>(transport);
    std::string movable = "move";
    if (portal::call_rpc_move(move_ctx, std::move(movable)) != "rpc:move") {
        return 10;
    }

    auto notify_ctx = portal::import_from<portal::import_symbols::rpc_notify, context_t>(transport);
    portal::call_rpc_notify(notify_ctx, 42);
    if (notified != 42) {
        return 4;
    }

    auto scale_ctx = portal::import_from<portal::import_symbols::rpc_scale, context_t>(transport);
    auto not_ctx = portal::import_from<portal::import_symbols::rpc_not, context_t>(transport);
    if (portal::call_rpc_scale(scale_ctx, 2.0) != 3.0
            || portal::call_rpc_not(not_ctx, true)) {
        return 9;
    }

    auto select_ctx = portal::import_from<portal::import_symbols::rpc_select, context_t>(transport);
    if (portal::call_rpc_select(select_ctx, -3) != -30
            || portal::call_rpc_select(select_ctx, 5u) != 1005) {
        return 5;
    }

    bool rejected = false;
    try {
        std::vector<portal::rpc::value> args{
            portal::rpc::value::boolean(true)
        };
        auto response = transport.round_trip(portal::rpc::detail::encode_request(
            portal::rpc::detail::symbol_id("rpc_select"), args));
        (void)portal::rpc::detail::decode_response(response);
    } catch (const portal::rpc::remote_error&) {
        rejected = true;
    }
    if (!rejected) {
        return 6;
    }

    bool missing = false;
    try {
        auto missing_ctx = portal::import_from<unknown_symbol, context_t>(transport);
        (void)context_t::backend_t::template invoke<int>(
            missing_ctx, portal::no_receiver_t{}, 1);
    } catch (const portal::rpc::remote_error&) {
        missing = true;
    }
    if (!missing) {
        return 7;
    }

    bool malformed = false;
    try {
        auto response = transport.round_trip(portal::rpc::bytes{1, 2, 3});
        (void)portal::rpc::detail::decode_response(response);
    } catch (const portal::rpc::remote_error&) {
        malformed = true;
    }
    if (!malformed) {
        return 8;
    }

    return 0;
}
