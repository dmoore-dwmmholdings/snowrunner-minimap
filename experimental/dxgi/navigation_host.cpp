#include "navigation_math.h"
#include <iostream>
#include <stdexcept>
void check(bool b,const char* name){if(!b)throw std::runtime_error(name);}
int main(){try{
 SrmmBearingFilter f;f.step(0,1000);float last=0;
 for(unsigned t=1016;t<=1160;t+=16){float a=f.step(1.57079632679f,t);check(a>last&&a<1.571f,"continuous turn without overshoot");last=a;}
 check(last>1.45f,"turn reaches over92percent in160ms");
 f.reset();f.step(3.13f,1000);check(std::abs(f.step(-3.13f,1016))>3.12f,"shortest arc across wrap");
 check(std::abs(f.step(.5f,2000)-.5f)<.001f,"stale gap resets");
 SrmmBearingFilter a,b;a.step(0,1000);b.step(0,1000);float av=0,bv=0;
 for(int i=1;i<=10;++i)av=a.step(1,1000+i*10);
 for(int i=1;i<=5;++i)bv=b.step(1,1000+i*20);
 check(std::abs(av-bv)<.0001f,"frame rate independent smoothing");
 auto p=srmm_project_destination(1,0,.001f,0,200,false);check(p&&p->edge&&std::abs(p->x-88)<.001,"east edge");
 p=srmm_project_destination(1,1,.001f,0,200,true);check(p&&p->edge&&p->x==88&&p->y==88,"square corner");
 p=srmm_project_destination(1,0,.001f,1.57079632679f,200,false);check(p&&std::abs(p->x)<.001&&p->y< -87,"rotation shared with map");
 p=srmm_project_destination(0,0,.001f,0,200,false);check(p&&!p->edge&&p->x==0&&p->y==0,"coincident destination");
 check(!srmm_project_destination(NAN,0,.001f,0,200,false),"nonfinite rejection");
 p=srmm_project_destination(.02f,.02f,.001f,1.57079632679f,200,false,12,.002f);
 check(p&&!p->edge&&std::abs(p->x-10)<.001f&&std::abs(p->y+20)<.001f,"rectangular map projects in world pixels before rotation");
 check(!srmm_project_destination(0,0,.001f,0,200,false,12,-.001f),"invalid vertical scale rejected");
 std::cout<<"PASS bearing interpolation and destination projection\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
