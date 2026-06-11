#include "dynabridge/backends/napi.h"

#include <type_traits>

static_assert(
    std::is_same<dynabridge::napi_backend::dynamic_value_t, napi_value>::value,
    "napi_backend should use the real N-API value type when compiled with node_api.h");

static_assert(
    std::is_same<dynabridge::backend_dynamic_value_t<dynabridge::napi_backend>, napi_value>::value,
    "napi_backend should expose napi_value as its backend dynamic value");

int main() {
    return 0;
}
