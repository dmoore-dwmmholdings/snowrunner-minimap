#include "candidate_packet.h"
#include "map_dds.h"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <fstream>
#include <stdexcept>
namespace {
unsigned checks{};
void check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
void put32(std::uint8_t* p,std::uint32_t value){for(unsigned i=0;i<4;++i)p[i]=static_cast<std::uint8_t>(value>>(i*8));}
void put64(std::uint8_t* p,std::uint64_t value){for(unsigned i=0;i<8;++i)p[i]=static_cast<std::uint8_t>(value>>(i*8));}
void putdouble(std::uint8_t* p,double value){std::uint64_t bits{};std::memcpy(&bits,&value,8);put64(p,bits);}
auto packet(){std::array<std::uint8_t,256> p{};std::memcpy(p.data(),"SRMMTEL1",8);put32(p.data()+8,1);put32(p.data()+12,256);put32(p.data()+16,1);put32(p.data()+20,42);put64(p.data()+24,1);put64(p.data()+32,1000);put64(p.data()+40,99);std::memcpy(p.data()+48,"level_test_01",13);putdouble(p.data()+144,100);putdouble(p.data()+160,200);putdouble(p.data()+184,1);return p;}
}
int main(int argc,char** argv){
 if(argc==3&&!std::strcmp(argv[1],"--decode")) {
    std::array<std::uint8_t,257> bytes{};std::ifstream input(argv[2],std::ios::binary);
    if(!input)return 2;input.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
    const auto parsed=srmm_parse_candidate(bytes.data(),static_cast<std::size_t>(input.gcount()));
    if(!parsed)return 1;
    std::cout<<parsed->pid<<" "<<parsed->session<<" "<<parsed->sequence<<" "<<parsed->sampled_ms<<" "<<parsed->map<<" "<<parsed->position[0]<<" "<<parsed->position[2]<<" "<<parsed->forward[2]<<"\n";return 0;
 }
 if(argc!=1)return 2;try {
    auto p=packet();auto parsed=srmm_parse_candidate(p.data(),p.size());check(parsed&&parsed->position[0]==100&&parsed->position[2]==200&&parsed->forward[2]==1,"Decode independent wire offsets");
    check(!srmm_parse_candidate(p.data(),255),"Reject truncated packet");
    auto bad=p;bad[192]=1;check(!srmm_parse_candidate(bad.data(),256),"Reject reserved data");
    bad=p;bad[62]=1;check(!srmm_parse_candidate(bad.data(),256),"Reject map padding contamination");
    bad=p;putdouble(bad.data()+144,std::nan(""));check(!srmm_parse_candidate(bad.data(),256),"Reject nonfinite position");
    bad=p;putdouble(bad.data()+184,0);check(!srmm_parse_candidate(bad.data(),256),"Reject zero direction");
    SrmmCalibration calibration;calibration.affine={.001,0,.25,0,-.001,.75};
    SrmmCandidateGate locked(42,99,{},"level_test_01",calibration);
    check(locked.consume(p.data(),256,1000).status==SrmmPacketStatus::unvalidated,"Unmeasured calibration hides candidates");
    SrmmCandidateGate preview(42,99,{},"level_test_01",calibration,true);
    auto preview_result=preview.consume(p.data(),256,1000);
    check(preview_result.status==SrmmPacketStatus::ready_alignment_preview&&preview_result.frame&&preview_result.frame->alignment_preview,"Opt-in preview is visibly and semantically provisional");
    check(!preview.consume(p.data(),256,1751).frame,"Preview still rejects stale telemetry");
    auto preview_bad=p;put64(preview_bad.data()+40,100);check(!preview.consume(preview_bad.data(),256,1752).frame,"Preview still rejects another session");
    calibration.validated=true;calibration.texture_v_flipped=true;
    SrmmCandidateGate gate(42,99,{},"level_test_01",calibration);
    auto result=gate.consume(p.data(),256,1000);check(result.frame&&std::abs(result.frame->center_u-.35f)<1e-6&&std::abs(result.frame->center_v-.55f)<1e-6,"Affine world projection");
    check(result.frame->heading==0&&result.frame->texture_v_flipped,"Heading independent of storage orientation");
    check(gate.consume(p.data(),256,1100).frame.has_value(),"Identical packet redraws while fresh");
    check(!gate.consume(p.data(),256,1751).frame,"Stale packet hides rather than freezes marker");
    put64(p.data()+24,2);put64(p.data()+32,1800);putdouble(p.data()+168,1);putdouble(p.data()+184,0);
    result=gate.consume(p.data(),256,1800);check(result.frame&&std::abs(result.frame->heading-1.57079632679f)<1e-6,"East heading clockwise");
    bad=p;putdouble(bad.data()+144,101);check(gate.consume(bad.data(),256,1801).status==SrmmPacketStatus::out_of_order,"Reject rewritten sequence");
    bad=p;put64(bad.data()+24,1);check(gate.consume(bad.data(),256,1802).status==SrmmPacketStatus::out_of_order,"Reject old sequence");
    bad=p;put64(bad.data()+24,3);put64(bad.data()+32,1799);check(gate.consume(bad.data(),256,1802).status==SrmmPacketStatus::out_of_order,"Reject timestamp regression despite increasing sequence");
    bad=p;put64(bad.data()+40,100);check(gate.consume(bad.data(),256,1803).status==SrmmPacketStatus::wrong_context,"Reject other publisher session");
    bad=p;bad[112]=1;check(gate.consume(bad.data(),256,1804).status==SrmmPacketStatus::wrong_context,"Reject different build");
    bad=p;bad[54]='x';check(gate.consume(bad.data(),256,1805).status==SrmmPacketStatus::wrong_context,"Reject changed map");
    bad=p;put64(bad.data()+32,2000);check(gate.consume(bad.data(),256,1806).status==SrmmPacketStatus::stale,"Reject future timestamp");
    put64(p.data()+24,3);put64(p.data()+32,1807);put32(p.data()+16,0);check(gate.consume(p.data(),256,1807).status==SrmmPacketStatus::unavailable,"Explicit read failure hides output");
    check(gate.consume(p.data(),256,1806).status==SrmmPacketStatus::clock_error,"Clock rollback poisons session");
    check(gate.consume(p.data(),256,2000).status==SrmmPacketStatus::clock_error,"Clock error requires new session");
    calibration.affine={0,0,0,0,0,0};SrmmCandidateGate singular(42,99,{},"level_test_01",calibration);p=packet();check(!singular.consume(p.data(),256,1000).frame,"Reject singular calibration");
    std::array<std::uint8_t,144> dds{};std::memcpy(dds.data(),"DDS ",4);put32(dds.data()+4,124);put32(dds.data()+8,0x81007);put32(dds.data()+12,4);put32(dds.data()+16,4);put32(dds.data()+20,16);put32(dds.data()+28,1);put32(dds.data()+76,32);put32(dds.data()+80,4);put32(dds.data()+84,0x35545844);put32(dds.data()+108,0x1000);
    auto map=srmm_parse_map_dds(dds.data(),dds.size());check(map&&map->width==4&&map->row_pitch==16&&map->size==16,"Parse bounded BC3 view");
    check(!srmm_parse_map_dds(dds.data(),143),"Reject truncated DDS");put32(dds.data()+16,8193);check(!srmm_parse_map_dds(dds.data(),144),"Reject oversized dimensions");put32(dds.data()+16,4);put32(dds.data()+28,2);check(!srmm_parse_map_dds(dds.data(),144),"Reject unsupported mips");put32(dds.data()+28,1);dds[112]=1;check(!srmm_parse_map_dds(dds.data(),144),"Reject cubemap flags");
    std::cout<<"PASS: "<<checks<<" CPU packet/calibration/DDS checks; no game access.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
