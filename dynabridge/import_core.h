#ifndef DYNABRIDGE_IMPORT_CORE_H
#define DYNABRIDGE_IMPORT_CORE_H

#include <utility>

#include "import_callable.h"

namespace dynabridge {
    template <typename Symbol, typename = void>
    struct import_symbol_traits {
        static const char* symbol_name() noexcept { return Symbol::symbol_name(); }
    };

    template <typename Symbol>
    struct import_symbol_traits<Symbol, void_t<typename Symbol::receiver_symbol_t>> {
        using receiver_symbol_t = typename Symbol::receiver_symbol_t;
        static const char* symbol_name() noexcept { return Symbol::symbol_name(); }
    };

    template <typename Symbol, typename Context, typename Source>
    Context import_from(Source& source) {
        static_assert(is_context_legal<Context>::value,
            "Your Context must define backend_t and backend_t must derive from backend_base<backend_t>.");
        return Context::backend_t::template import_from<Symbol, Context>(
            source,
            import_symbol_traits<Symbol>::symbol_name());
    }

    template <template <typename> class Delegate, typename Context, typename Object>
    auto bind(Context& ctx, Object&& object) {
        return Delegate<Context>(ctx, std::forward<Object>(object));
    }

    template <template <typename> class Delegate, typename Context, typename... Args>
    auto bind_receiver(Context& ctx, Args&&... args) {
        using receiver_t = Delegate<Context>;
        using object_t = typename Context::backend_t::template object_t<receiver_t, import_t>;
        return bind<Delegate>(ctx, object_t(std::forward<Args>(args)...));
    }

    template <template <typename> class>
    struct import_class_traits;

    template <template <typename> class Delegate, typename Context, typename... Args>
    auto construct(Context& ctx, Args&&... args) {
        return import_class_traits<Delegate>::construct(ctx, std::forward<Args>(args)...);
    }
}

#endif //DYNABRIDGE_IMPORT_CORE_H
