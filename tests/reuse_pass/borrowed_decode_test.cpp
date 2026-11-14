#include "fixtures.h"
#include "benchmarks/reuse_pass/experiments/borrowed_decode.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"CHECK %s line %d\n",#c,__LINE__);std::abort();}}while(false)
namespace r=portal::rpc;
r::value own(const reuse_experiment::view_value& v) {
    using K=r::value_kind;
    switch(v.kind){
    case K::undefined:return {};
    case K::signed_integer:{std::int64_t n;std::memcpy(&n,&v.bits,8);return r::value::signed_integer(n);}
    case K::unsigned_integer:return r::value::unsigned_integer(v.bits);
    case K::real:{double n;std::memcpy(&n,&v.bits,8);return r::value::real(n);}
    case K::boolean:return r::value::boolean(v.bits!=0);
    case K::string:return r::value::string(std::string(v.data,v.size));
    }
    std::abort();
}
void compare(const r::bytes& frame){
    r::detail::request original;reuse_experiment::request_view views;
    bool a=r::detail::try_decode_request(frame,original),b=reuse_experiment::decode(frame,views);
    CHECK(a==b);
    if(!a)return;
    CHECK(original.method==views.method && original.args.size()==views.args.size());
    std::vector<r::value> restored;
    for(const auto& v:views.args)restored.push_back(own(v));
    CHECK(r::detail::encode_request(original.method,original.args)==r::detail::encode_request(views.method,restored));
}
int main(){
    std::mt19937_64 gen(613);
    for(unsigned i=0;i<2000;++i){
        std::vector<r::value> values;
        const auto count=gen()%12;
        for(unsigned j=0;j<count;++j){
            switch(gen()%6){
            case 0:values.push_back(r::value{});break;
            case 1:values.push_back(r::value::signed_integer(static_cast<std::int64_t>(gen())));break;
            case 2:values.push_back(r::value::unsigned_integer(gen()));break;
            case 3:values.push_back(r::value::real(static_cast<double>(gen())));break;
            case 4:values.push_back(r::value::boolean(gen()%2));break;
            case 5:{std::string s(gen()%1024,'\0');for(auto& c:s)c=static_cast<char>(gen());values.push_back(r::value::string(std::move(s)));break;}
            }
        }
        auto frame=r::detail::encode_request(gen(),values);compare(frame);
        for(unsigned j=0;j<8;++j){
            auto modified=frame;
            if(j%2)modified.resize(gen()%modified.size());
            else modified[gen()%modified.size()]=static_cast<std::uint8_t>(gen());
            compare(modified);
        }
    }
    compare({});
    std::puts("PASS: borrowed prototype vs owning decoder, 2000 valid + 16000 mutated frames");
}
