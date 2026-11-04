#include "menu_state_packet.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>
namespace {
int checks=0;
void check(bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);}
void put32(std::array<std::uint8_t,128>& b,unsigned o,std::uint32_t n){for(unsigned i=0;i<4;++i)b[o+i]=static_cast<std::uint8_t>(n>>(8*i));}
void put64(std::array<std::uint8_t,128>& b,unsigned o,std::uint64_t n){for(unsigned i=0;i<8;++i)b[o+i]=static_cast<std::uint8_t>(n>>(8*i));}
std::array<std::uint8_t,128> sample(unsigned state,std::uint64_t seq,std::uint64_t tick){std::array<std::uint8_t,128> b{};std::memcpy(b.data(),"SRMMUI01",8);put32(b,8,1);put32(b,12,128);put32(b,16,state);put32(b,20,42);put64(b,24,seq);put64(b,32,tick);put64(b,40,99);for(unsigned i=48;i<80;++i)b[i]=0xab;return b;}
}
int main(int argc,char** argv){try{
 if(argc==2){std::ifstream file(argv[1],std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});auto p=srmm_parse_ui_state(bytes.data(),bytes.size());check(p.has_value(),"cross-language parse");check(p->pid==42&&p->session==99&&p->sequence==5&&p->sampled_ms==1000&&p->state==SrmmUiState::menu&&p->build[0]==0xab,"cross-language fields");}
 else {
 auto b=sample(2,1,1000);auto p=srmm_parse_ui_state(b.data(),b.size());check(p&&p->state==SrmmUiState::menu,"valid menu");
 check(!srmm_parse_ui_state(nullptr,128)&&!srmm_parse_ui_state(b.data(),127),"size/null rejection");
 for(unsigned o:{0u,8u,12u,16u,20u,24u,40u,80u,127u}){auto invalid=b;invalid[o]=o==16?3:o==20||o==24||o==40?0:0xff;check(!srmm_parse_ui_state(invalid.data(),invalid.size()),"malformed header/identity/reserved");}
 std::array<std::uint8_t,32> hash{};hash.fill(0xab);SrmmUiStateGate gate(42,99,hash);
 auto result=gate.consume(b.data(),b.size(),1000);check(result.status==SrmmUiPacketStatus::ready&&result.state==SrmmUiState::menu,"menu admitted");
 check(gate.consume(b.data(),b.size(),1001).state==SrmmUiState::menu,"unchanged packet can repeat");
 auto mutation=b;put32(mutation,16,1);check(gate.consume(mutation.data(),mutation.size(),1002).status==SrmmUiPacketStatus::out_of_order,"same-sequence mutation rejected");
 b=sample(1,2,1100);check(gate.consume(b.data(),b.size(),1100).state==SrmmUiState::gameplay,"return to gameplay no latch");
 result=gate.consume(b.data(),b.size(),1851);check(result.status==SrmmUiPacketStatus::stale&&result.state==SrmmUiState::unknown,"stale never permits editing");
 b=sample(2,3,1900);check(gate.consume(b.data(),b.size(),1900).state==SrmmUiState::menu,"fresh recovery");
 b=sample(0,4,1901);check(gate.consume(b.data(),b.size(),1901).state==SrmmUiState::unknown,"explicit unknown");
 b=sample(2,5,2000);put64(b,40,100);check(gate.consume(b.data(),b.size(),2000).status==SrmmUiPacketStatus::wrong_context,"session mismatch");
 b=sample(2,6,2100);check(gate.consume(b.data(),b.size(),2050).status==SrmmUiPacketStatus::stale,"future timestamp");
 check(gate.consume(b.data(),b.size(),2049).status==SrmmUiPacketStatus::clock_error,"backward local clock poisons gate");
 check(gate.consume(b.data(),b.size(),2100).state==SrmmUiState::unknown,"clock failure remains unknown");
 }
 std::cout<<"PASS: "<<checks<<" UI packet checks; owned data only.\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
