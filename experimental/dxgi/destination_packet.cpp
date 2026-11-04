#include "destination_packet.h"
#include "navigation_math.h"
#include <cmath>
#include <cstring>
#include <utility>
namespace {
std::uint32_t u32(const std::uint8_t* p){return p[0]|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}
std::uint64_t u64(const std::uint8_t* p){return u32(p)|(std::uint64_t(u32(p+4))<<32);}
double f64(const std::uint8_t* p){const auto bits=u64(p);double value{};std::memcpy(&value,&bits,8);return value;}
bool map_name(const std::string& value){if(value.size()<9||value.size()>54||value.compare(0,6,"level_"))return false;for(char c:value)if(!(c>='a'&&c<='z')&&!(c>='0'&&c<='9')&&c!='_')return false;return true;}
}
std::optional<SrmmDestinationPacket> srmm_parse_destinations(const void* data,std::size_t size) noexcept {
 try {
    if(!data||size!=SrmmDestinationPacketSize)return {};
    const auto* p=static_cast<const std::uint8_t*>(data);
    if(std::memcmp(p,"SRMMDST1",8)||u32(p+8)!=1||u32(p+12)!=SrmmDestinationPacketSize||u32(p+16)>1||u32(p+152)!=1)return {};
    SrmmDestinationPacket result;
    result.available=u32(p+16)!=0;result.pid=u32(p+20);result.sequence=u64(p+24);result.sampled_ms=u64(p+32);result.session=u64(p+40);
    unsigned length=0;while(length<64&&p[48+length])++length;
    if(length==64)return {};for(unsigned i=length;i<64;++i)if(p[48+i])return {};
    result.map.assign(reinterpret_cast<const char*>(p+48),length);std::memcpy(result.build.data(),p+112,32);
    result.save_modified_unix_ms=u64(p+144);result.count=u32(p+156);
    if(!result.pid||!result.sequence||!result.session||!map_name(result.map)||result.count>16)return {};
    if(result.available?!result.save_modified_unix_ms:(result.count||result.save_modified_unix_ms))return {};
    for(unsigned i=0;i<16;++i){
        const auto* point=p+160+i*32;
        if(i>=result.count){for(unsigned j=0;j<32;++j)if(point[j])return {};continue;}
        if((u32(point)!=1&&u32(point)!=2)||u32(point+4)!=i+1||u64(point+24))return {};
        const double x=f64(point+8),z=f64(point+16);
        if(!std::isfinite(x)||!std::isfinite(z)||std::abs(x)>20000||std::abs(z)>20000)return {};
        result.points[i]={i+1,x,z,u32(point)};
    }
    return result;
 }catch(...){return {};}
}
SrmmDestinationGate::SrmmDestinationGate(std::uint32_t pid,std::uint64_t session,std::array<std::uint8_t,32> build,std::string map)
 :pid_(pid),session_(session),build_(build),map_(std::move(map)){}
SrmmDestinationFrame SrmmDestinationGate::consume(const void* data,std::size_t size,std::uint64_t now) noexcept {
    auto hide=[](SrmmDestinationStatus s){SrmmDestinationFrame f;f.status=s;return f;};
    if(poisoned_||(have_tick_&&now<last_tick_)){poisoned_=true;return hide(SrmmDestinationStatus::clock_error);}
    have_tick_=true;last_tick_=now;
    const auto packet=srmm_parse_destinations(data,size);if(!packet)return hide(SrmmDestinationStatus::invalid);
    const auto& p=*packet;
    if(p.pid!=pid_||p.session!=session_||p.build!=build_||p.map!=map_)return hide(SrmmDestinationStatus::wrong_context);
    if(p.sampled_ms>now||now-p.sampled_ms>1500)return hide(SrmmDestinationStatus::stale);
    if(have_sequence_&&(p.sequence<last_sequence_||p.sampled_ms<last_sampled_||(p.sequence==last_sequence_&&std::memcmp(data,accepted_.data(),SrmmDestinationPacketSize))))return hide(SrmmDestinationStatus::out_of_order);
    last_sequence_=p.sequence;last_sampled_=p.sampled_ms;have_sequence_=true;std::memcpy(accepted_.data(),data,SrmmDestinationPacketSize);
    if(!p.available)return hide(SrmmDestinationStatus::unavailable);
    return {SrmmDestinationStatus::saved_snapshot,p.save_modified_unix_ms,p.count,p.points};
}
void srmm_place_saved_waypoints(SrmmMapFrame& frame,const SrmmDestinationFrame& destinations,const SrmmCalibration& calibration) noexcept {
    frame.destination_count=0;
    if(destinations.status!=SrmmDestinationStatus::saved_snapshot||destinations.count>16||(!calibration.validated&&!frame.alignment_preview))return;
    const auto& a=calibration.affine;
    for(double value:a)if(!std::isfinite(value))return;
    for(unsigned i=0;i<destinations.count;++i){
        const auto& p=destinations.points[i];
        const double u=a[0]*p.x+a[1]*p.z+a[2],v=a[3]*p.x+a[4]*p.z+a[5];
        const auto projected=srmm_project_destination(static_cast<float>(u-frame.center_u),static_cast<float>(v-frame.center_v),frame.uv_per_pixel,frame.map_rotation,frame.size,frame.square,12,frame.uv_per_pixel_v);
        if(projected)frame.destinations[frame.destination_count++]={projected->x,projected->y,projected->bearing,projected->edge,p.kind};
    }
}
