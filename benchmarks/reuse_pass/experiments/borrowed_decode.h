#ifndef PORTAL_EXPERIMENT_BORROWED_DECODE_H
#define PORTAL_EXPERIMENT_BORROWED_DECODE_H
// Isolated representation experiment; deliberately not a public backend API.
// Every text slice borrows the immutable input frame. Owning conversion still
// copies its bytes. No generic/custom binder receives these views.
#include <cstdint>
#include <vector>
namespace reuse_experiment {
struct view_value {
    std::uint64_t bits=0;
    const char* data=nullptr;
    std::uint32_t size=0;
    portal::rpc::value_kind kind=portal::rpc::value_kind::undefined;
};
struct request_view {
    std::uint64_t method=0;
    std::vector<view_value> args;
};
inline bool decode(const portal::rpc::bytes& frame,request_view& result) {
    using K=portal::rpc::value_kind;
    std::size_t offset=0;
    auto read=[&](unsigned width,std::uint64_t& value){
        if(width>frame.size()-offset)return false;
        value=0;
        for(unsigned i=0;i<width;++i)value|=static_cast<std::uint64_t>(frame[offset+i])<<(8*i);
        offset+=width;return true;
    };
    result.args.clear();
    std::uint64_t magic=0,version=0,argc=0;
    if(!read(4,magic)||!read(1,version)||magic!=portal::rpc::detail::request_magic||
        version!=portal::rpc::detail::protocol_version||!read(8,result.method)||
        !read(4,argc)||argc>frame.size()-offset)return false;
    result.args.reserve(static_cast<std::size_t>(argc));
    for(std::uint64_t i=0;i<argc;++i) {
        view_value v;std::uint64_t tag=0;
        if(!read(1,tag))return false;
        v.kind=static_cast<K>(tag);
        switch(v.kind){
        case K::undefined:break;
        case K::signed_integer:case K::unsigned_integer:case K::real:
            if(!read(8,v.bits))return false;
            break;
        case K::boolean:
            if(!read(1,v.bits))return false;
            v.bits=(v.bits!=0);break;
        case K::string:{
            std::uint64_t n=0;
            if(!read(4,n)||n>frame.size()-offset)return false;
            v.data=reinterpret_cast<const char*>(frame.data()+offset);
            v.size=static_cast<std::uint32_t>(n);offset+=static_cast<std::size_t>(n);break;
        }
        default:return false;
        }
        result.args.push_back(v);
    }
    return offset==frame.size();
}
}
#endif
