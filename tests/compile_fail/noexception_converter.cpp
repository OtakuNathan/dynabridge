#include "../../dynabridge/traits.h"
#include "../../dynabridge/optional.h"
#ifdef ANNOTATED_CONVERTER
#define CONVERTER_NOEXCEPT noexcept
#else
#define CONVERTER_NOEXCEPT
#endif
struct backend {
    template<class T> struct converter {
        template<class C> static dynabridge::optional<T> from(C&,int v) CONVERTER_NOEXCEPT {return dynabridge::optional<T>(v);}
        template<class C> static int to(C&,T v) CONVERTER_NOEXCEPT {return v;}
    };
};
struct context {using backend_t=backend;};
int main() {
    context ctx;
#ifdef CHECK_TO
    return dynabridge::to_cast<int>(ctx,1)-1;
#else
    return *dynabridge::from_optional<int>(ctx,1)-1;
#endif
}
