#include <cstring>
#include <functional>
#include <memory>
#include <utility>

#define DYNABRIDGE_IMPORT_DEF "tests/custom_import.def"
#define DYNABRIDGE_EXPORT_DEF "tests/custom_export.def"
#include "dynabridge/bridge.h"
#undef DYNABRIDGE_IMPORT_DEF
#undef DYNABRIDGE_EXPORT_DEF

#define DYNABRIDGE_IMPORT_DEF "tests/custom_second_import.def"
#define DYNABRIDGE_EXPORT_DEF "tests/custom_second_export.def"
#include "dynabridge/bridge.h"
#undef DYNABRIDGE_IMPORT_DEF
#undef DYNABRIDGE_EXPORT_DEF

#define DYNABRIDGE_IMPORT_DEF "tests/import.def"
#define DYNABRIDGE_EXPORT_DEF "tests/export.def"
#include "dynabridge/bridge.h"
#undef DYNABRIDGE_IMPORT_DEF
#undef DYNABRIDGE_EXPORT_DEF

#include "fake_backend.h"

namespace {
    struct import_callable {
        int first = 0;
        int second = 0;

        int operator()(int a) {
            first = a;
            return first;
        }

        int operator()(int a, unsigned b) {
            first = a;
            second = static_cast<int>(b);
            return first + second;
        }
    };

    int custom_bar_function(int a, unsigned b) {
        return a * static_cast<int>(b);
    }

    int custom_qux_function(int value) {
        return value * 10;
    }

    struct custom_module {
        int def_count = 0;
        std::function<int(int, unsigned)> custom_bar;
        std::function<int(int)> custom_qux;

        template <typename Binder>
        void def(const char* name, Binder binder) {
            def_impl(name, std::move(binder), dynabridge::type_identity<typename Binder::signature_t>{});
        }

        template <typename Binder>
        void def_impl(const char* name, Binder binder, dynabridge::type_identity<int(int, unsigned)>) {
            ++def_count;
            if (std::strcmp(name, "custom_bar") == 0) {
                auto shared = std::make_shared<Binder>(std::move(binder));
                custom_bar = [shared](int a, unsigned b) {
                    return (*shared)(a, b);
                };
            }
        }

        template <typename Binder>
        void def_impl(const char* name, Binder binder, dynabridge::type_identity<int(int)>) {
            ++def_count;
            if (std::strcmp(name, "custom_qux") == 0) {
                auto shared = std::make_shared<Binder>(std::move(binder));
                custom_qux = [shared](int value) {
                    return (*shared)(value);
                };
            }
        }
    };
}

int main() {
    using context_t = dynabridge::fake_backend::context_t<import_callable>;

    context_t ctx(import_callable{});
    const int imported_result = dynabridge::call_custom_foo(ctx, 5, 7u);
    if (imported_result != 12
            || ctx.callable_.first != 5
            || ctx.callable_.second != 7
            || ctx.to_count != 2
            || ctx.from_count != 1) {
        return 1;
    }

    ctx.reset_conversions();
    const int second_imported_result = dynabridge::call_custom_baz(ctx, 9);
    if (second_imported_result != 9
            || ctx.callable_.first != 9
            || ctx.callable_.second != 7
            || ctx.to_count != 1
            || ctx.from_count != 1) {
        return 4;
    }

    ctx.reset_conversions();
    const int default_imported_result = dynabridge::call_calc(ctx, 2, 3u);
    if (default_imported_result != 5
            || ctx.callable_.first != 2
            || ctx.callable_.second != 3
            || ctx.to_count != 2
            || ctx.from_count != 1) {
        return 6;
    }

    custom_module module;
    dynabridge::export_custom_bar(ctx, module, custom_bar_function);
    dynabridge::export_custom_qux(ctx, module, custom_qux_function);
    if (!module.custom_bar || !module.custom_qux || module.def_count != 2) {
        return 2;
    }

    ctx.reset_conversions();
    const int exported_result = module.custom_bar(6, 7u);
    if (exported_result != 42 || ctx.to_count != 1 || ctx.from_count != 2) {
        return 3;
    }

    ctx.reset_conversions();
    const int second_exported_result = module.custom_qux(8);
    if (second_exported_result != 80 || ctx.to_count != 1 || ctx.from_count != 1) {
        return 5;
    }

    return 0;
}
