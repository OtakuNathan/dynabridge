#include "fixture.h"
#include <new>
static thread_local long countdown=-1;
void* operator new(std::size_t n){if(countdown==0)throw std::bad_alloc();if(countdown>0)--countdown;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
int main(){
 tr::router r;bind(r);tr::loopback_transport t(r);std::string input(1024,'a');int failed=0,succeeded=0;
 for(int point=0;point<12;++point){
  tr::workspace cache;
  countdown=point;
  try {auto result=tr::call<Echo>(t,cache,{},input);countdown=-1;CHECK(result&&result.value==input);++succeeded;}
  catch(const std::bad_alloc&){countdown=-1;++failed;}
  // An exception must not poison/reuse live storage in the next call.
  auto next=tr::call<Echo>(t,cache,{},input);CHECK(next&&next.value==input);
 }
 CHECK(failed>0&&succeeded>0);
 // Full decode before dispatch: valid first payload, invalid second argument.
 using Two=tr::method<0x201,1,w::i32,w::blob<2048>,w::boolean>;
 int calls=0;CHECK(r.define<Two>([&](std::string,bool){++calls;return std::int32_t(1);})==w::errc::ok);
 w::bytes req,reply;CHECK(tr::encode_request<Two>(req,{},input,true)==w::errc::ok);req.back()=2;
 CHECK(r.dispatch_into(req,reply)==w::errc::invalid_value);CHECK(calls==0);
 auto response=tr::decode_response<Two>(w::view(reply));CHECK(!response&&response.error==w::errc::invalid_value);
 std::puts("allocation failure and pre-dispatch validation: passed");
}
