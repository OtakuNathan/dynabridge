#ifndef DYNABRIDGE_BACKENDS_NAPI_CONVERTERS_H
#define DYNABRIDGE_BACKENDS_NAPI_CONVERTERS_H

#include <cstdint>
#include <limits>

namespace dynabridge {
    template <>
    struct napi_backend::converter<int> {
        static napi_value to(context_t& ctx, int value) {
            napi_value result = nullptr;
            napi_backend::check(
                napi_create_int32(ctx.env(), value, &result),
                "napi_create_int32 failed");
            return result;
        }

        static optional<int> from(context_t& ctx, napi_value value) {
            int result = 0;
            if (napi_get_value_int32(ctx.env(), value, &result) != napi_ok) {
                return optional<int>();
            }
            return optional<int>(result);
        }
    };

    template <>
    struct napi_backend::converter<unsigned> {
        static napi_value to(context_t& ctx, unsigned value) {
            napi_value result = nullptr;
            napi_backend::check(
                napi_create_uint32(ctx.env(), value, &result),
                "napi_create_uint32 failed");
            return result;
        }

        static optional<unsigned> from(context_t& ctx, napi_value value) {
            std::int64_t result = 0;
            if (napi_get_value_int64(ctx.env(), value, &result) != napi_ok
                    || result < 0
                    || static_cast<std::uint64_t>(result)
                        > static_cast<std::uint64_t>(std::numeric_limits<unsigned>::max())) {
                return optional<unsigned>();
            }
            return optional<unsigned>(static_cast<unsigned>(result));
        }
    };
}

#endif //DYNABRIDGE_BACKENDS_NAPI_CONVERTERS_H
