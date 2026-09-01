#include "examples/common/native.h"

#define DYNABRIDGE_EXPORT_DEF "examples/common/export.def"
#include "dynabridge/bridge.h"
#include "dynabridge/backends/napi.h"

#include <exception>

namespace {
    napi_value init(napi_env env, napi_value exports) {
        try {
            static dynabridge::napi_backend::export_context_t ctx;
            ctx = dynabridge::napi_backend::export_context_t(env);
            dynabridge::napi_backend::module_t module{env, exports};

            dynabridge::export_add(ctx, module, dynabridge::example_native::add);
            dynabridge::exports::counter::register_all(ctx, module);
            return exports;
        } catch (const std::exception& error) {
            napi_throw_error(env, nullptr, error.what());
            return nullptr;
        } catch (...) {
            napi_throw_error(env, nullptr, "dynabridge example initialization failed");
            return nullptr;
        }
    }
}

NAPI_MODULE(dynabridge_napi_example, init)
