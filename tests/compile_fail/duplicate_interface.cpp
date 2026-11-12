#define PORTAL_IMPORT_DEF "../tests/compile_fail/duplicate_interface.def"
#include "../../portal/portal.h"

struct compile_context {
    struct backend_t {
        template <typename, typename>
        struct object_t {
        };
    };
};

static_assert(sizeof(portal::subject<compile_context>) > 0, "");

int main() {
}
