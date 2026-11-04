#include "game_session.h"
#include <iostream>
#include <stdexcept>
namespace {int checks=0;void check(bool b,const char* s){++checks;if(!b)throw std::runtime_error(s);}}
int main(){try{
 std::string valid="version=1\nenabled=1\nmap_path=C:\\fixture\\map.dds\npacket_path=C:\\fixture\\packet.bin\nmap_sha256="+std::string(64,'a')+"\nsession=18446744073709551615\nmap=level_test_01\naffine=.001 0 .5 0 -.001 .5\nuv_per_pixel=.001\ntexture_v_flipped=1\ngeometry=alignment_preview\nviewport=32 32 320\n";
 SrmmSessionConfig c;check(srmm_parse_session(valid,c),"valid explicit config");check(c.session==~std::uint64_t(0)&&!c.calibration.validated,"uint64 identity and preview distinction");
 check(!srmm_parse_session(valid+"enabled=1\n",c),"duplicate rejection");check(!srmm_parse_session(valid+"extra=1\n",c),"unknown field rejection");check(!srmm_parse_session(valid+std::string(1,'\0'),c),"nul rejection");check(!srmm_parse_session(std::string(16385,'x'),c),"size bound");
 for(auto replacement:{"0","-1","18446744073709551616","garbage"}){auto b=valid;auto p=b.find("18446744073709551615");b.replace(p,20,replacement);check(!srmm_parse_session(b,c),"invalid session identity");}
 for(auto replacement:{"nan 0 .5 0 -.001 .5","0 0 0 0 0 0",".001 0 .5 0 -.001 .5 extra"}){auto b=valid;auto p=b.find(".001 0 .5 0 -.001 .5");b.replace(p,19,replacement);check(!srmm_parse_session(b,c),"invalid affine");}
 check(srmm_parse_session(valid+"zoom=2\ntracking=motion\nshape=square\n",c)&&c.zoom==2&&c.tracking==2&&c.square,"display options");
 for(auto bad:{"zoom=0\n","tracking=bad\n","shape=triangle\n"})check(!srmm_parse_session(valid+bad,c),"invalid display option");
 auto b=valid;b.replace(b.find("geometry=alignment_preview"),26,"geometry=measured");check(srmm_parse_session(b,c)&&c.calibration.validated,"measured is explicit");
 std::cout<<"PASS: "<<checks<<" session parser checks; no game access.\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
