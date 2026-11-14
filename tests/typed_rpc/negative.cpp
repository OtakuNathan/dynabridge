#include "portal/rpc/typed.h"
namespace w=portal::wire;
namespace tr=portal::typed_rpc;
struct Bad{std::uint32_t a=0,b=0;};
#if NEGATIVE_CASE == 0
#define F(X,T) X(T,1,a,w::u32) X(T,2,b,w::u32)
PORTAL_WIRE_RECORD(Bad,1,1,F);
#elif NEGATIVE_CASE == 1
#define F(X,T) X(T,1,a,w::u32) X(T,1,b,w::u32)
PORTAL_WIRE_RECORD(Bad,1,1,F);
#elif NEGATIVE_CASE == 2
#define F(X,T) X(T,1,a,w::u64)
PORTAL_WIRE_RECORD(Bad,1,1,F);
#endif
int main(){
#if NEGATIVE_CASE == 0
 w::bytes out;Bad b;
 if(w::encode_into<w::record<Bad>>(b,out)!=w::errc::ok) return 1;
 auto decoded=w::decode<w::record<Bad>>(w::view(out));
 using M=tr::method<1,1,w::i32,w::i32>;tr::router r;
 if(r.define<M>([](std::int32_t v){return v;})!=w::errc::ok) return 2;
 if(tr::encode_request<M>(out,{},std::int32_t(7))!=w::errc::ok) return 3;
 return decoded ? 0 : 4;
#elif NEGATIVE_CASE == 3
 w::bytes out;Bad b;w::encode_into<w::record<Bad>>(b,out);
#elif NEGATIVE_CASE == 4
 using M=tr::method<1,1,w::i32,w::i32>;w::bytes out;
 tr::encode_request<M>(out,{},std::int64_t(7));
#elif NEGATIVE_CASE == 5
 using M=tr::method<1,1,w::i32,w::i32>;tr::router r;
 r.define<M>([](std::int32_t v){return std::int64_t(v);});
#elif NEGATIVE_CASE == 6
 w::sequence<w::boolean,4>::value_type x;
 (void)x;
#endif
}
