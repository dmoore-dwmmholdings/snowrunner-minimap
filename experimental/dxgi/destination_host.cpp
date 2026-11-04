#include "destination_packet.h"
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
unsigned checks=0;
void check(bool b,const char* text){++checks;if(!b)throw std::runtime_error(text);}
using Bytes=std::array<std::uint8_t,SrmmDestinationPacketSize>;
void word(Bytes& b,unsigned offset,std::uint64_t v,unsigned n=4){for(unsigned i=0;i<n;++i)b[offset+i]=static_cast<std::uint8_t>(v>>(i*8));}
void number(Bytes& b,unsigned offset,double value){std::memcpy(b.data()+offset,&value,8);}
Bytes packet(){Bytes b{};std::memcpy(b.data(),"SRMMDST1",8);word(b,8,1);word(b,12,b.size());word(b,16,1);word(b,20,42);word(b,24,1,8);word(b,32,1000,8);word(b,40,99,8);std::memcpy(b.data()+48,"level_test_01",13);word(b,144,1700000000000ULL,8);word(b,152,1);word(b,156,1);word(b,160,1);word(b,164,1);number(b,168,20);number(b,176,0);return b;}
}
int main(int argc,char** argv){try{
 if(argc==3&&std::strcmp(argv[1],"--packet")==0){std::ifstream input(argv[2],std::ios::binary|std::ios::ate);check(bool(input)&&input.tellg()==static_cast<std::streamoff>(SrmmDestinationPacketSize),"wire file size");input.seekg(0);Bytes data{};input.read(reinterpret_cast<char*>(data.data()),data.size());const auto parsed=srmm_parse_destinations(data.data(),data.size());check(bool(parsed),"wire parse");std::cout<<"PASS saved waypoint wire; count="<<parsed->count<<"\n";return 0;}
 check(argc==1,"unexpected arguments");auto good=packet();auto parsed=srmm_parse_destinations(good.data(),good.size());
 check(parsed&&parsed->count==1&&parsed->points[0].x==20,"valid saved snapshot");
 auto objective=good;word(objective,160,2);auto objectiveParsed=srmm_parse_destinations(objective.data(),objective.size());check(objectiveParsed&&objectiveParsed->points[0].kind==2,"saved objective kind preserved");
 check(!srmm_parse_destinations(good.data(),good.size()-1),"truncated packet");
 for(auto offset:{0u,8u,12u,16u,152u,156u,160u,164u,184u,200u}){auto bad=good;word(bad,offset,99);check(!srmm_parse_destinations(bad.data(),bad.size()),"malformed or unused data rejected");}
 auto bad=good;number(bad,168,NAN);check(!srmm_parse_destinations(bad.data(),bad.size()),"nonfinite coordinate");
 bad=good;number(bad,176,20001);check(!srmm_parse_destinations(bad.data(),bad.size()),"coordinate bound");
 SrmmDestinationGate gate(42,99,{},"level_test_01");
 auto admitted=gate.consume(good.data(),good.size(),1000);check(admitted.status==SrmmDestinationStatus::saved_snapshot&&admitted.save_modified_unix_ms==1700000000000ULL,"saved timestamp preserved");
 check(gate.consume(good.data(),good.size(),1001).status==SrmmDestinationStatus::saved_snapshot,"exact packet can be rendered repeatedly");
 bad=good;number(bad,168,21);check(gate.consume(bad.data(),bad.size(),1002).status==SrmmDestinationStatus::out_of_order,"same sequence mutation rejected");
 bad=good;word(bad,40,100,8);check(gate.consume(bad.data(),bad.size(),1003).status==SrmmDestinationStatus::wrong_context,"wrong session hidden");
 check(gate.consume(good.data(),good.size(),2501).status==SrmmDestinationStatus::stale,"stopped source expires");
 auto fresh=good;word(fresh,24,2,8);word(fresh,32,2502,8);check(gate.consume(fresh.data(),fresh.size(),2502).status==SrmmDestinationStatus::saved_snapshot,"fresh source recovers after stale");
 word(fresh,24,3,8);word(fresh,16,0);word(fresh,144,0,8);word(fresh,156,0);std::memset(fresh.data()+160,0,512);
 check(gate.consume(fresh.data(),fresh.size(),2503).status==SrmmDestinationStatus::unavailable,"explicit source unavailable clears destinations");
 check(gate.consume(good.data(),good.size(),2000).status==SrmmDestinationStatus::clock_error,"clock reversal fails closed");
 SrmmMapFrame frame;frame.center_u=frame.center_v=.5f;frame.uv_per_pixel=.001f;frame.uv_per_pixel_v=.002f;frame.size=200;frame.map_rotation=1.57079632679f;
 SrmmCalibration calibration;calibration.affine={.001,0,.5,0,-.002,.5};calibration.validated=true;
 srmm_place_saved_waypoints(frame,admitted,calibration);check(frame.destination_count==1&&std::abs(frame.destinations[0].x)<.001f&&std::abs(frame.destinations[0].y+20)<.001f,"saved coordinate follows map rotation");
 admitted.points[0].x=2000;srmm_place_saved_waypoints(frame,admitted,calibration);check(frame.destination_count==1&&frame.destinations[0].edge&&std::abs(frame.destinations[0].y+88)<.001f,"offscreen waypoint clamps to edge");
 calibration.validated=false;srmm_place_saved_waypoints(frame,admitted,calibration);check(frame.destination_count==0,"unvalidated geometry hidden outside preview");
 frame.alignment_preview=true;srmm_place_saved_waypoints(frame,admitted,calibration);check(frame.destination_count==1,"explicit alignment preview allowed");
 admitted.status=SrmmDestinationStatus::stale;srmm_place_saved_waypoints(frame,admitted,calibration);check(frame.destination_count==0,"stale state cannot retain markers");
 std::cout<<"PASS "<<checks<<" saved waypoint wire, admission and projection checks; original fixtures only.\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
