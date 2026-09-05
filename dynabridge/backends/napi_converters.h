#ifndef DYNABRIDGE_BACKENDS_NAPI_CONVERTERS_H
#define DYNABRIDGE_BACKENDS_NAPI_CONVERTERS_H

#include <cstddef>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

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
            double result = 0;
            if (napi_get_value_double(ctx.env(), value, &result) != napi_ok
                    || !std::isfinite(result) || std::trunc(result) != result
                    || result < (std::numeric_limits<int>::min)()
                    || result > (std::numeric_limits<int>::max)()) {
                return optional<int>();
            }
            return optional<int>(static_cast<int>(result));
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
            double result = 0;
            if (napi_get_value_double(ctx.env(), value, &result) != napi_ok
                    || !std::isfinite(result) || std::trunc(result) != result
                    || result < 0
                    || result > (std::numeric_limits<unsigned>::max)()) {
                return optional<unsigned>();
            }
            return optional<unsigned>(static_cast<unsigned>(result));
        }
    };

    template <>
    struct napi_backend::converter<std::string> {
        static napi_value to(context_t& ctx, const std::string& value) {
            napi_value result = nullptr;
            napi_backend::check(
                napi_create_string_utf8(
                    ctx.env(), value.data(), value.size(), &result),
                "napi_create_string_utf8 failed");
            return result;
        }

        static optional<std::string> from(context_t& ctx, napi_value value) {
            napi_valuetype type = napi_undefined;
            napi_backend::check(
                napi_typeof(ctx.env(), value, &type),
                "napi_typeof failed during string conversion");
            if (type != napi_string) {
                return optional<std::string>();
            }

            std::size_t size = 0;
            napi_backend::check(
                napi_get_value_string_utf8(ctx.env(), value, nullptr, 0, &size),
                "napi_get_value_string_utf8 size probe failed");
            if (size == (std::numeric_limits<std::size_t>::max)()) {
                throw std::length_error("dynabridge N-API string is too large");
            }
            std::string result(size + 1, '\0');
            std::size_t copied = 0;
            napi_backend::check(
                napi_get_value_string_utf8(
                    ctx.env(), value, &result[0], result.size(), &copied),
                "napi_get_value_string_utf8 failed");
            result.resize(copied);
            return optional<std::string>(std::move(result));
        }
    };
}

#endif //DYNABRIDGE_BACKENDS_NAPI_CONVERTERS_H
