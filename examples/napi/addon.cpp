#include "examples/common/native.h"

#define DYNABRIDGE_EXPORT_DEF "examples/common/export.def"
#include "dynabridge/bridge.h"
#include "dynabridge/backends/napi.h"

#include <exception>
#include <memory>

namespace {
    napi_value init(napi_env env, napi_value exports) {
        try {
            // Every addon instance has its own env, including Worker instances.
            // The callbacks borrow ctx, so keep it alive until that env exits.
            using context_t = dynabridge::napi_backend::export_context_t;
            std::unique_ptr<context_t> owner(new context_t(env));
            dynabridge::napi_backend::check(napi_add_env_cleanup_hook(
                env, [](void* data) { delete static_cast<context_t*>(data); }, owner.get()),
                "napi_add_env_cleanup_hook failed for export context");
            auto& ctx = *owner.release();
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
