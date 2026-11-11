#define PORTAL_EXPORT_DEF "export.def"
#include <portal/bridge.h>

#include <type_traits>

int main() {
    static_assert(
        std::is_same<portal::free_callable<int(int)>,
                     portal::callable<portal::no_receiver_t, int(int)>>::value,
        "installed target must expose the public bridge headers");

    auto builder = portal::bind_package_ping();
    (void)builder;
}
