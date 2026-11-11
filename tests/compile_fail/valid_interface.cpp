#define PORTAL_METHOD_INDEX __LINE__
#define PORTAL_IMPORT_DEF "../tests/compile_fail/valid_interface.def"
#include "../../portal/bridge.h"

struct compile_context {
    struct backend_t {
        template <typename, typename>
        struct object_t {
        };
    };
};

static_assert(sizeof(portal::subject<compile_context>) > 0,
    "a valid generated interface must compile");

int main() {
}
