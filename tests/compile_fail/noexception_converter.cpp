#include "../../portal/traits.h"
#include "../../portal/optional.h"
#ifdef ANNOTATED_CONVERTER
#define CONVERTER_NOEXCEPT noexcept
#else
#define CONVERTER_NOEXCEPT
#endif
struct backend {
    template<class T> struct converter {
        template<class C> static portal::optional<T> from(C&,int v) CONVERTER_NOEXCEPT {return portal::optional<T>(v);}
        template<class C> static int to(C&,T v) CONVERTER_NOEXCEPT {return v;}
    };
};
struct context {using backend_t=backend;};
int main() {
    context ctx;
#ifdef CHECK_TO
    return portal::to_cast<int>(ctx,1)-1;
#else
    return *portal::from_optional<int>(ctx,1)-1;
#endif
}
