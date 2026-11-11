#ifndef DYNABRIDGE_EXTENSIONS_FLUX_FOUNDRY_UV_EXECUTOR_H
#define DYNABRIDGE_EXTENSIONS_FLUX_FOUNDRY_UV_EXECUTOR_H

#include <executor/uv_executor.h>

// Compatibility name only. New code should include <executor/uv_executor.h>
// and use flux_foundry::uv_executor directly. All behavior belongs to FF:
// bounded admission, retained try_dispatch rejection, and draining close.
namespace dynabridge {
    namespace flux_foundry_extensions {
        using uv_executor = flux_foundry::uv_executor;
    }
}

#endif // DYNABRIDGE_EXTENSIONS_FLUX_FOUNDRY_UV_EXECUTOR_H
