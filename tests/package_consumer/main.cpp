#define DYNABRIDGE_EXPORT_DEF "export.def"
#include <dynabridge/bridge.h>

#include <type_traits>

int main() {
    static_assert(
        std::is_same<dynabridge::free_callable<int(int)>,
                     dynabridge::callable<dynabridge::no_receiver_t, int(int)>>::value,
        "installed target must expose the public bridge headers");

    auto builder = dynabridge::bind_package_ping();
    (void)builder;
}
