#include <portal/rpc/typed.h>

#include <algorithm>
#include <vector>

struct metadata_record { std::uint32_t number = 0; };
#define METADATA_FIELDS(X, T) X(T, 1, number, portal::wire::u32)
PORTAL_WIRE_RECORD(metadata_record, 42, 1, METADATA_FIELDS);

template<class Codec>
bool check_minimum(std::size_t expected) {
    // std::max binds both inputs by reference, requiring a definition in C++14.
    return (std::max)(Codec::min_size, std::size_t(0)) == expected;
}

bool check_metadata() {
    namespace w = portal::wire;
    using method = portal::typed_rpc::method<42, 1, w::u32>;
    using field = w::member<metadata_record, 1, w::u32, &metadata_record::number>;
    std::vector<std::uint64_t> values;
    values.emplace_back(method::id);
    values.emplace_back(method::revision);
    values.emplace_back(method::arity);
    values.emplace_back(field::id);
    values.emplace_back(w::end_field::id);
    return values == std::vector<std::uint64_t>({42, 1, 0, 1, 0})
        && check_minimum<w::u32>(4) && check_minimum<w::boolean>(1)
        && check_minimum<w::f64>(8) && check_minimum<w::none>(0)
        && check_minimum<w::blob<1024>>(4) && check_minimum<w::array<w::u32, 2>>(8)
        && check_minimum<w::sequence<w::u32, 8>>(4) && check_minimum<w::maybe<w::u32>>(1)
        && check_minimum<field>(4) && check_minimum<w::record<metadata_record>>(4)
        && check_minimum<w::end_field>(0);
}
