#include "../../tests/typed_rpc/fixture.h"
#ifdef PORTAL_TYPED_RPC_ISOLATED_LEGACY
#include "isolated_rpc.h"
#else
#include "portal/export_callable.h"
#include "portal/backends/rpc.h"
#endif
#include "portal/backends/rpc_reuse.h"
#include <chrono>
#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <new>
#include <atomic>

#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif
namespace pr=portal::rpc;
namespace allocation_counter {
static thread_local bool enabled=false;
static thread_local std::uint64_t count=0,total=0;
}
#ifdef PORTAL_TYPED_COUNT_ALLOCATIONS
void* operator new(std::size_t n){
 if(void* p=std::malloc(n?n:1)){
  if(allocation_counter::enabled){++allocation_counter::count;allocation_counter::total+=n;}
  return p;
 }
 throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
#endif

NOINLINE std::int64_t add_body(std::int64_t a,std::int64_t b) noexcept{return a+b;}
NOINLINE std::string echo_body(std::string s) noexcept{return s;}
struct old_add {
 using signature_t=std::int64_t(std::int64_t,std::int64_t);
 portal::optional<pr::value> call_optional(const pr::value& a,const pr::value& b){
  if(a.kind()!=pr::value_kind::signed_integer||b.kind()!=pr::value_kind::signed_integer)return {};
  return pr::value::signed_integer(add_body(a.as_signed(),b.as_signed()));
 }
};
struct old_echo {
 using signature_t=std::string(std::string);
 portal::optional<pr::value> call_optional(pr::value& a){
  if(a.kind()!=pr::value_kind::string)return {};
  return pr::value::string(echo_body(a.take_string()));
 }
};

// Factor control only: known-signature decoding with the EXACT original wire
// tags/envelopes. It bypasses dynamic values on the server, but is monomorphic;
// it is not a new general router implementation or an overload-equivalence claim.
template<class C,std::uint8_t Tag> struct legacy_field {
 using value_type=typename C::value_type;
 static constexpr std::size_t min_size=C::min_size+1;
 static constexpr std::uint64_t fingerprint() noexcept{return C::fingerprint();}
 static bool size(w::measure& s,const value_type& v) noexcept{return s.add(1)&&C::size(s,v);}
 static bool write(w::writer& out,const value_type& v) noexcept{return out.integer(Tag)&&C::write(out,v);}
 static bool read(w::reader& in,value_type& v){
  std::uint8_t tag=0;return in.integer(tag)&&(tag==Tag||in.fail(w::errc::invalid_value))&&C::read(in,v);
 }
};
using LInt=legacy_field<w::i64,static_cast<std::uint8_t>(pr::value_kind::signed_integer)>;
using LString=legacy_field<w::blob<1048576>,static_cast<std::uint8_t>(pr::value_kind::string)>;
using LAdd=tr::method<1,1,LInt,LInt,LInt>;
using LEcho=tr::method<2,1,LString,LString>;
template<class M,class F> struct legacy_static_transport {
 std::uint64_t id;F function;
 void round_trip_into(const w::bytes& request,w::bytes& response){
  w::reader in{w::view(request)};std::uint32_t magic=0,count=0;std::uint64_t method=0;std::uint8_t version=0;
  CHECK(in.integer(magic)&&in.integer(version)&&in.integer(method)&&in.integer(count));
  CHECK(magic==pr::detail::request_magic&&version==1&&method==id&&count==M::arity);
  typename M::arguments args{};using ix=std::make_index_sequence<M::arity>;
  CHECK(M::read_args(in,args,ix{})&&in.empty());
  auto result=invoke(args,ix{});using C=typename M::return_codec;
  w::measure size(1048576);CHECK(size.add(6)&&C::size(size,result));
  response.resize(size.size());w::writer out(response.data(),response.size());
  CHECK(out.integer(pr::detail::response_magic)&&out.integer<std::uint8_t>(1)&&out.integer<std::uint8_t>(0)&&C::write(out,result)&&out.empty());
 }
 template<std::size_t... I> typename M::result_type invoke(typename M::arguments& args,std::index_sequence<I...>){return function(std::move(std::get<I>(args))...);}
};

// Optimistic per-method reference. Same typed envelope, signature/budget checks,
// owning values and native body, but no general routing table or entry vtable.
struct hand_add_transport {
 void round_trip_into(const w::bytes& request,w::bytes& response){
  tr::envelope e;CHECK(tr::read_envelope(w::view(request),tr::request_magic,e)==w::errc::ok);
  CHECK(e.method_id==Add64::id&&e.revision==1&&e.fingerprint==Add64::fingerprint());
  w::reader in(e.payload);std::int64_t a=0,b=0;CHECK(w::i64::read(in,a)&&w::i64::read(in,b)&&in.empty());
  auto sum=add_body(a,b);CHECK(tr::encode_response<Add64>(response,sum)==w::errc::ok);
 }
};
struct hand_echo_transport {
 void round_trip_into(const w::bytes& request,w::bytes& response){
  tr::envelope e;CHECK(tr::read_envelope(w::view(request),tr::request_magic,e)==w::errc::ok);
  CHECK(e.method_id==Echo::id&&e.revision==1&&e.fingerprint==Echo::fingerprint());
  w::reader in(e.payload);std::string a;CHECK(w::blob<1048576>::read(in,a)&&in.empty());
  auto result=echo_body(std::move(a));CHECK(tr::encode_response<Echo>(response,result)==w::errc::ok);
 }
};
template<class T> struct transport_boundary {
 T& backend;
 NOINLINE void round_trip_into(const w::bytes& request,w::bytes& response) {backend.round_trip_into(request,response);}
};
NOINLINE pr::bytes fresh_boundary(pr::router& server,const pr::bytes& request){return server.dispatch(request);}

struct environment {
 pr::router legacy;
 tr::router typed;
 pr::reusing_loopback_transport old_reuse;
 pr::call_workspace old_storage;
 tr::loopback_transport new_transport;
 tr::workspace new_storage;
 legacy_static_transport<LAdd,decltype(&add_body)> static_add;
 legacy_static_transport<LEcho,decltype(&echo_body)> static_echo;
 hand_add_transport hand_add;hand_echo_transport hand_echo;
 std::uint64_t add_id=pr::detail::symbol_id("add"),echo_id=pr::detail::symbol_id("echo");
 explicit environment(bool scalar,std::size_t budget)
 :old_reuse(legacy),old_storage(budget),new_transport(typed),new_storage(budget),
  static_add{pr::detail::symbol_id("add"),&add_body},static_echo{pr::detail::symbol_id("echo"),&echo_body}{
   if(scalar){legacy.define("add",old_add{});CHECK(typed.define<Add64>(&add_body)==w::errc::ok);}
   else{legacy.define("echo",old_echo{});CHECK(typed.define<Echo>(&echo_body)==w::errc::ok);}
 }
};
template<int Lane> NOINLINE std::uint64_t scalar(environment& e,std::string&,std::uint64_t i){
 const auto a=static_cast<std::int64_t>(i&32767),b=static_cast<std::int64_t>((i*7)&32767);
 std::int64_t value=0;
 if(Lane==0){
  auto req=pr::detail::encode_request_values(e.add_id,pr::value::signed_integer(a),pr::value::signed_integer(b));
  auto reply=fresh_boundary(e.legacy,req);auto r=pr::detail::decode_response(reply);CHECK(r.kind()==pr::value_kind::signed_integer);value=r.as_signed();
 }else if(Lane==1){
  transport_boundary<decltype(e.old_reuse)> boundary{e.old_reuse};
  auto r=pr::round_trip_reusing(boundary,e.old_storage,e.add_id,pr::value::signed_integer(a),pr::value::signed_integer(b));CHECK(r.kind()==pr::value_kind::signed_integer);value=r.as_signed();
 }else if(Lane==2){
  transport_boundary<decltype(e.static_add)> boundary{e.static_add};
  auto r=pr::round_trip_reusing(boundary,e.old_storage,e.add_id,pr::value::signed_integer(a),pr::value::signed_integer(b));CHECK(r.kind()==pr::value_kind::signed_integer);value=r.as_signed();
 }else if(Lane==3||Lane==4){
  transport_boundary<decltype(e.new_transport)> boundary{e.new_transport};
  auto r=tr::call<Add64>(boundary,e.new_storage,{},a,b);CHECK(r);value=r.value;
 }else if(Lane==5){
  transport_boundary<decltype(e.hand_add)> boundary{e.hand_add};
  auto r=tr::call<Add64>(boundary,e.new_storage,{},a,b);CHECK(r);value=r.value;
 }else std::abort();
 CHECK(value==a+b);return static_cast<std::uint64_t>(value);
}
template<int Lane> NOINLINE std::uint64_t payload(environment& e,std::string& input,std::uint64_t i){
 input[0]=static_cast<char>(i&255);std::string value;
 if(Lane==0){
  auto req=pr::detail::encode_request_values(e.echo_id,pr::value::borrowed_string(input));
  auto reply=fresh_boundary(e.legacy,req);auto r=pr::detail::decode_response(reply);CHECK(r.kind()==pr::value_kind::string);value=r.take_string();
 }else if(Lane==1){
  transport_boundary<decltype(e.old_reuse)> boundary{e.old_reuse};
  auto r=pr::round_trip_reusing(boundary,e.old_storage,e.echo_id,pr::value::borrowed_string(input));CHECK(r.kind()==pr::value_kind::string);value=r.take_string();
 }else if(Lane==2){
  transport_boundary<decltype(e.static_echo)> boundary{e.static_echo};
  auto r=pr::round_trip_reusing(boundary,e.old_storage,e.echo_id,pr::value::borrowed_string(input));CHECK(r.kind()==pr::value_kind::string);value=r.take_string();
 }else if(Lane==3||Lane==4){
  transport_boundary<decltype(e.new_transport)> boundary{e.new_transport};
  auto r=tr::call<Echo>(boundary,e.new_storage,{},input);CHECK(r);value=std::move(r.value);
 }else if(Lane==5){
  transport_boundary<decltype(e.hand_echo)> boundary{e.hand_echo};
  auto r=tr::call<Echo>(boundary,e.new_storage,{},input);CHECK(r);value=std::move(r.value);
 }else std::abort();
 CHECK(value==input);return value.size()+static_cast<unsigned char>(value.front());
}
int main(int argc,char** argv){
 if(argc!=5){std::cerr<<"usage: probe lane payload_size iterations timing|allocation\n";return 2;}
 const std::string lane=argv[1];const auto size=std::stoull(argv[2]),n=std::stoull(argv[3]);
 if(!n||size>65536){return 2;}
 environment e(size==0,lane=="typed_fresh"?0:1024*1024);std::string input(size,'x');
 using function=std::uint64_t(*)(environment&,std::string&,std::uint64_t);
 function selected=nullptr;
 const char* names[]={"dynamic_fresh","dynamic_reuse","legacy_static","typed_reuse","typed_fresh","hand_reference"};
 function scalars[]={&scalar<0>,&scalar<1>,&scalar<2>,&scalar<3>,&scalar<4>,&scalar<5>};
 function payloads[]={&payload<0>,&payload<1>,&payload<2>,&payload<3>,&payload<4>,&payload<5>};
 for(unsigned k=0;k<6;++k)if(lane==names[k])selected=size?payloads[k]:scalars[k];
 if(!selected)return 2;
 auto one=[&](std::uint64_t i){return selected(e,input,i);};
 for(unsigned i=0;i<4096;++i)(void)one(i);
 if(std::string(argv[4])=="allocation") {
#ifdef PORTAL_TYPED_COUNT_ALLOCATIONS
  allocation_counter::enabled=true;
#else
  std::cerr<<"allocation mode requires PORTAL_TYPED_COUNT_ALLOCATIONS\n";return 2;
#endif
 }
 const auto start=std::chrono::steady_clock::now();std::uint64_t sum=0;
 for(std::uint64_t i=0;i<n;++i)sum+=one(i);
 const auto stop=std::chrono::steady_clock::now();allocation_counter::enabled=false;
 auto ns=std::chrono::duration<double,std::nano>(stop-start).count()/n;
 std::cout<<std::setprecision(12)<<"{\"lane\":\""<<lane<<"\",\"payload\":"<<size<<",\"iterations\":"<<n
          <<",\"ns_per_call\":"<<ns<<",\"checksum\":"<<sum<<",\"allocations\":"<<allocation_counter::count
          <<",\"requested_bytes\":"<<allocation_counter::total<<"}\n";
}
