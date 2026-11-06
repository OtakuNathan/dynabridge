#if RETURN_KIND == 0
using result_t = int;
#elif RETURN_KIND == 1
using result_t = int&;
#elif RETURN_KIND == 2
using result_t = const int&;
#else
using result_t = int&&;
#endif
#if SCHEMA_EXPORT
#define DYNABRIDGE_EXPORT_DEF "../tests/compile_fail/return_reference_schema.def"
#else
#define DYNABRIDGE_IMPORT_DEF "../tests/compile_fail/return_reference_schema.def"
#endif
#include "../../dynabridge/bridge.h"
int main() {}
