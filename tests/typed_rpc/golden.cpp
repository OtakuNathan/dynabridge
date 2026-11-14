#include "fixture.h"
#include <iostream>
#include <iomanip>
void hex(const w::bytes& b){for(auto c:b)std::cout<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(c);std::cout<<'\n';}
int main(){
 w::bytes out;
 CHECK(tr::encode_request<Add>(out,{},std::int32_t(-2),std::int32_t(0x01020304))==w::errc::ok);hex(out);
 CHECK(w::encode_into<w::record<Packet>>(packet(),out)==w::errc::ok);hex(out);
 std::cout<<std::hex<<w::record<Packet>::fingerprint()<<'\n';
 CHECK(tr::encode_response<Add>(out,std::int32_t(42))==w::errc::ok);hex(out);
 tr::envelope envelope;envelope.method_id=Add::id;envelope.revision=Add::revision;envelope.fingerprint=Add::fingerprint();
 CHECK(tr::error_reply(out,envelope,w::errc::handler_failed)==w::errc::handler_failed);hex(out);
}
