#define DYNABRIDGE_IMPORT_DEF "../tests/compile_fail/duplicate_method.def"
#include "../../dynabridge/bridge.h"

struct compile_context {
    struct backend_t {
        template <typename, typename>
        struct object_t {
        };
    };
};

static_assert(sizeof(dynabridge::subject<compile_context>) > 0, "");

int main() {
}
