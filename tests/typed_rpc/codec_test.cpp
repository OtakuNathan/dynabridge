#include "fixture.h"
#include <cmath>
#include <limits>
#include <random>
#include <thread>
#include <atomic>

struct Outer { Packet packet; std::uint32_t marker=0; };
#define OUTER_FIELDS(X,T) X(T,1,packet,w::record<Packet>) X(T,2,marker,w::u32)
PORTAL_WIRE_RECORD(Outer,0x1002,1,OUTER_FIELDS);
struct Empty {};
#define EMPTY_FIELDS(X,T)
PORTAL_WIRE_RECORD(Empty,0x1003,1,EMPTY_FIELDS);

static void scalars(){
 w::bytes bytes;
 CHECK(w::encode_into<w::i32>(-2,bytes)==w::errc::ok);
 CHECK(bytes==w::bytes({0xfe,0xff,0xff,0xff}));
 CHECK(w::decode<w::i32>(w::view(bytes)).value==-2);
 std::mt19937_64 rng(0x7479706564ull);
 for(unsigned i=0;i<20000;++i){
  auto n=rng();CHECK(w::encode_into<w::u64>(n,bytes)==w::errc::ok);
  auto u=w::decode<w::u64>(w::view(bytes));CHECK(u&&u.value==n);
  auto s=static_cast<std::int64_t>(n>>1);if(i&1)s=-s;
  CHECK(w::encode_into<w::i64>(s,bytes)==w::errc::ok);
  auto v=w::decode<w::i64>(w::view(bytes));CHECK(v&&v.value==s);
 }
 for(auto n:{(std::numeric_limits<std::int64_t>::min)(),(std::numeric_limits<std::int64_t>::max)(),std::int64_t(0),std::int64_t(-1)}){
  CHECK(w::encode_into<w::i64>(n,bytes)==w::errc::ok);CHECK(w::decode<w::i64>(w::view(bytes)).value==n);
 }
 for(int n=-128;n<=127;++n){CHECK(w::encode_into<w::i8>(static_cast<std::int8_t>(n),bytes)==w::errc::ok);CHECK(w::decode<w::i8>(w::view(bytes)).value==n);}
 for(int n=-32768;n<=32767;n+=31){CHECK(w::encode_into<w::i16>(static_cast<std::int16_t>(n),bytes)==w::errc::ok);CHECK(w::decode<w::i16>(w::view(bytes)).value==n);}
 bytes={2};auto b=w::decode<w::boolean>(w::view(bytes));CHECK(!b&&b.error==w::errc::invalid_value);
 for(auto bits:{0ull,0x8000000000000000ull,0x7ff0000000000000ull,0xfff0000000000000ull,0x7ff8000000000001ull}){
  double d;std::memcpy(&d,&bits,8);CHECK(w::encode_into<w::f64>(d,bytes)==w::errc::ok);
  auto value=w::decode<w::f64>(w::view(bytes));CHECK(value);std::uint64_t back;std::memcpy(&back,&value.value,8);CHECK(back==bits);
 }
 CHECK(!w::decode<w::u32>(w::view(nullptr,4)));
 CHECK(w::decode<w::none>(w::view{}));
}
static void records(){
 w::bytes bytes;Packet p=packet();
 CHECK(w::encode_into<w::record<Packet>>(p,bytes)==w::errc::ok);
 auto v=w::decode<w::record<Packet>>(w::view(bytes));CHECK(v&&same(p,v.value));
 for(std::size_t n=0;n<bytes.size();++n){auto bad=w::decode<w::record<Packet>>(w::view(bytes.data(),n));CHECK(!bad);CHECK(bad.value.payload.empty());}
 auto trailing=bytes;trailing.push_back(0);CHECK(!w::decode<w::record<Packet>>(w::view(trailing)));
 const auto original=bytes;p.payload.assign(4097,'x');CHECK(w::encode_into<w::record<Packet>>(p,bytes)==w::errc::limit_exceeded);CHECK(bytes==original);
 p=packet();p.scale.present=false;CHECK(w::encode_into<w::record<Packet>>(p,bytes)==w::errc::ok);
 auto absent=w::decode<w::record<Packet>>(w::view(bytes));CHECK(absent&&same(p,absent.value));
 w::limits l;l.max_depth=0;CHECK(!w::decode<w::record<Packet>>(w::view(bytes),l));
 l={};l.max_elements=2;CHECK(!w::decode<w::record<Packet>>(w::view(bytes),l));
 l={};l.max_decoded_bytes=1;CHECK(!w::decode<w::record<Packet>>(w::view(bytes),l));
 l={};l.max_frame_bytes=1;CHECK(!w::decode<w::record<Packet>>(w::view(bytes),l));
 Outer o;o.packet=packet();o.marker=100;
 CHECK(w::encode_into<w::record<Outer>>(o,bytes)==w::errc::ok);
 auto outer=w::decode<w::record<Outer>>(w::view(bytes));CHECK(outer&&same(o.packet,outer.value.packet)&&outer.value.marker==100);
 l={};l.max_depth=1;CHECK(!w::decode<w::record<Outer>>(w::view(bytes),l));
 // Count budget protects sequences of zero-byte records, not just byte lengths.
 bytes={64,0,0,0};l={};l.max_elements=63;
 auto many=w::decode<w::sequence<w::record<Empty>,64>>(w::view(bytes),l);CHECK(!many);
 bytes={255,255,255,255};CHECK(!w::decode<w::sequence<w::u32,64>>(w::view(bytes)));
}
static void protocol(){
 tr::router server;int calls=0;
 CHECK(server.define<Add>([&](std::int32_t a,std::int32_t b){++calls;return a+b;})==w::errc::ok);
 CHECK(server.define<Add>([](std::int32_t a,std::int32_t b){return a-b;})==w::errc::invalid_value);
 CHECK(server.size()==1);
 w::bytes req,reply;
 CHECK(tr::encode_request<Add>(req,{},std::int32_t(20),std::int32_t(22))==w::errc::ok);
 CHECK(req.size()==tr::header_size+8);
 CHECK(w::bytes(req.begin()+32,req.end())==w::bytes({20,0,0,0,22,0,0,0}));
 CHECK(req[0]=='P'&&req[1]=='T'&&req[2]=='R'&&req[3]=='1');
 const auto golden=req;
 CHECK(server.dispatch_into(req,reply)==w::errc::ok);CHECK(calls==1);
 auto result=tr::decode_response<Add>(w::view(reply));CHECK(result&&result.value==42);
 for(std::size_t n=0;n<golden.size();++n){req.assign(golden.begin(),golden.begin()+n);CHECK(server.dispatch_into(req,reply)!=w::errc::ok);CHECK(calls==1);}
 for(auto off:{0u,4u,6u,8u,16u,20u,24u}){req=golden;req[off]^=0x80;CHECK(server.dispatch_into(req,reply)!=w::errc::ok);CHECK(calls==1);}
 req=golden;req.push_back(0);CHECK(server.dispatch_into(req,reply)==w::errc::malformed);CHECK(calls==1);
 req=golden;CHECK(server.dispatch_into(req,req)==w::errc::ok);CHECK(calls==2);CHECK(tr::decode_response<Add>(w::view(req)).value==42);
 using other=tr::method<Add::id,Add::revision,w::u32,w::u32,w::u32>;
 CHECK(tr::encode_request<other>(req,{},std::uint32_t(1),std::uint32_t(2))==w::errc::ok);
 CHECK(server.dispatch_into(req,reply)==w::errc::incompatible_schema);CHECK(calls==2);
 auto mismatch=tr::decode_response<other>(w::view(reply));CHECK(!mismatch&&mismatch.error==w::errc::incompatible_schema);
 tr::router all;bind(all);tr::loopback_transport t(all);tr::workspace storage;
 auto ping=tr::call<Ping>(t,storage,{});CHECK(ping);
 auto echo=tr::call<Echo>(t,storage,{},std::string("\0\xfe",2));CHECK(echo&&echo.value==std::string("\0\xfe",2));
 auto owned=tr::call<EchoPacket>(t,storage,{},packet());CHECK(owned);storage.release_cached_storage();CHECK(same(owned.value,packet()));
 // Fixed revision mismatch is explicit; it is not an automatic schema migration.
 using next=tr::method<Add::id,2,w::i32,w::i32,w::i32>;
 auto next_result=tr::call<next>(t,storage,{},std::int32_t(1),std::int32_t(2));CHECK(!next_result);
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
 tr::router throwing;CHECK(throwing.define<Add>([](std::int32_t,std::int32_t)->std::int32_t{throw std::runtime_error("private details");})==w::errc::ok);
 tr::loopback_transport failure(throwing);auto err=tr::call<Add>(failure,storage,{},std::int32_t(1),std::int32_t(2));CHECK(!err&&err.error==w::errc::handler_failed);
#endif
}
static void reentrancy_and_threads(){
 tr::router server;tr::workspace shared_worker;tr::loopback_transport transport(server);
 CHECK(server.define<Echo>([&](std::string s){
  auto nested=tr::call<Add>(transport,shared_worker,{},std::int32_t(20),std::int32_t(22));CHECK(nested&&nested.value==42);return s;
 })==w::errc::ok);
 CHECK(server.define<Add>([](std::int32_t a,std::int32_t b) noexcept{return a+b;})==w::errc::ok);
 for(unsigned i=0;i<100;++i){auto r=tr::call<Echo>(transport,shared_worker,{},std::string(1000,'z'));CHECK(r&&r.value==std::string(1000,'z'));}
 CHECK(shared_worker.retained_capacity()<=2*65536);
 tr::workspace no_cache(0);CHECK(tr::call<Add>(transport,no_cache,{},std::int32_t(2),std::int32_t(3)));CHECK(no_cache.retained_capacity()==0);
 tr::workspace small(64);CHECK(tr::call<Echo>(transport,small,{},std::string(1000,'z')));CHECK(small.retained_capacity()<=128);
 std::atomic<int> count{0};std::vector<std::thread> workers;
 for(int j=0;j<4;++j)workers.emplace_back([&,j]{
  tr::workspace w;tr::loopback_transport t(server);
  for(int i=0;i<5000;++i){auto r=tr::call<Add>(t,w,{},std::int32_t(i),std::int32_t(j));CHECK(r&&r.value==i+j);++count;}
 });
 for(auto& t:workers) { t.join(); }
 CHECK(count==20000);
}
static void mutation(){
 // Deterministic malformed-frame campaign with small allocation budgets.
 std::mt19937 rng(0x53434845);tr::router server;bind(server);w::bytes original,reply;
 CHECK(tr::encode_request<EchoPacket>(original,{},packet())==w::errc::ok);
 w::limits l;l.max_frame_bytes=2048;l.max_decoded_bytes=8192;l.max_elements=128;l.max_depth=8;
 for(unsigned i=0;i<20000;++i){
  auto frame=original;
  for(unsigned k=0;k<1+i%5;++k)frame[rng()%frame.size()]=static_cast<std::uint8_t>(rng());
  if(i%3==0)frame.resize(rng()%(original.size()+1));
  const auto status=server.dispatch_into(frame,reply,l);
  if(status==w::errc::ok){auto r=tr::decode_response<EchoPacket>(w::view(reply),l);CHECK(r);}
 }
}
int main(){scalars();records();protocol();reentrancy_and_threads();mutation();std::puts("typed RPC codec/router: passed");}
