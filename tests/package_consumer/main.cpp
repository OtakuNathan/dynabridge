#define PORTAL_EXPORT_DEF "export.def"
#include <portal/portal.h>
#include <portal/backends/rpc.h>
#include <portal/backends/rpc_reuse.h>
#include <portal/rpc/typed_async.h>

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

struct packet {
    std::uint32_t sequence = 0;
    std::string payload;
};
#define PACKAGE_PACKET_FIELDS(X, T) \
    X(T, 1, sequence, portal::wire::u32) \
    X(T, 2, payload, portal::wire::blob<1024>)
PORTAL_WIRE_RECORD(packet, 1, 1, PACKAGE_PACKET_FIELDS);

bool check_metadata();

int main() {
    if (!check_metadata()) return 4;
    using metadata_method = portal::typed_rpc::method<42, 1, portal::wire::u32>;
    std::vector<std::uint64_t> ids;
    ids.emplace_back(metadata_method::id);
    if (ids.front() != 42) return 5;
    static_assert(
        std::is_same<portal::free_callable<int(int)>,
                     portal::callable<portal::no_receiver_t, int(int)>>::value,
        "installed target must expose the public bridge headers");

    auto builder = portal::bind_package_ping();
    (void)builder;

    portal::rpc_backend::export_context_t context;
    portal::rpc::router dynamic_server;
    dynamic_server.define("ping", portal::create_export_callable_binder<int(int)>(
        context, [](int value) noexcept { return value + 1; }));
    portal::rpc::reusing_loopback_transport dynamic_transport(dynamic_server);
    portal::rpc::call_workspace dynamic_storage;
    auto dynamic_result = portal::rpc::round_trip_reusing(dynamic_transport, dynamic_storage,
        portal::rpc::detail::symbol_id("ping"), portal::rpc::value::signed_integer(41));
    if (dynamic_result.as_signed() != 42) return 1;

    namespace typed = portal::typed_rpc;
    using echo = typed::method<1, 1, portal::wire::record<packet>, portal::wire::record<packet>>;
    typed::router server;
    if (server.define<echo>([](packet value) noexcept { return value; }) != typed::errc::ok) return 2;
    typed::loopback_transport transport(server);
    typed::workspace storage;
    packet input;
    input.sequence = 42;
    input.payload = std::string("a\0b", 3);
    auto result = typed::call<echo>(transport, storage, {}, input);
    return result && result.value.sequence == input.sequence && result.value.payload == input.payload ? 0 : 3;
}
