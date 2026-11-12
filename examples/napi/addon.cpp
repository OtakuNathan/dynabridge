#include "examples/common/native.h"

#define PORTAL_EXPORT_DEF "examples/common/export.def"
#include "portal/portal.h"
#include "portal/backends/napi.h"

#include <exception>
#include <memory>

namespace {
    napi_value init(napi_env env, napi_value exports) {
        try {
            // Every addon instance has its own env, including Worker instances.
            // The callbacks borrow ctx, so keep it alive until that env exits.
            using context_t = portal::napi_backend::export_context_t;
            std::unique_ptr<context_t> owner(new context_t(env));
            portal::napi_backend::check(napi_add_env_cleanup_hook(
                env, [](void* data) { delete static_cast<context_t*>(data); }, owner.get()),
                "napi_add_env_cleanup_hook failed for export context");
            auto& ctx = *owner.release();
            portal::napi_backend::module_t module{env, exports};

            portal::export_add(ctx, module, portal::example_native::add);
            portal::exports::counter::register_all(ctx, module);
            return exports;
        } catch (const std::exception& error) {
            napi_throw_error(env, nullptr, error.what());
            return nullptr;
        } catch (...) {
            napi_throw_error(env, nullptr, "portal example initialization failed");
            return nullptr;
        }
    }
}

NAPI_MODULE(portal_napi_example, init)
