#include "candidate_packet.h"
#include <cmath>
#include <cstring>
#include <utility>
namespace {
std::uint32_t u32(const std::uint8_t* p){return p[0]|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}
std::uint64_t u64(const std::uint8_t* p){return u32(p)|(std::uint64_t(u32(p+4))<<32);}
double f64(const std::uint8_t* p){const auto bits=u64(p);double value{};std::memcpy(&value,&bits,8);return value;}
bool valid_map(const std::string& value){if(value.size()<9||value.size()>54||value.compare(0,6,"level_"))return false;for(char c:value)if(!(c>='a'&&c<='z')&&!(c>='0'&&c<='9')&&c!='_')return false;return true;}
}
std::optional<SrmmCandidatePacket> srmm_parse_candidate(const void* data,std::size_t size) noexcept {
 try {
    if(!data||size!=256)return {};
    const auto* p=static_cast<const std::uint8_t*>(data);
    if(std::memcmp(p,"SRMMTEL1",8)||u32(p+8)!=1||u32(p+12)!=256||u32(p+16)>1)return {};
    for(unsigned i=192;i<256;++i)if(p[i])return {};
    unsigned length=0;while(length<64&&p[48+length])++length;
    if(length==64)return {};for(unsigned i=length;i<64;++i)if(p[48+i])return {};
    SrmmCandidatePacket result;
    result.available=u32(p+16)!=0;result.pid=u32(p+20);result.sequence=u64(p+24);result.sampled_ms=u64(p+32);result.session=u64(p+40);
    result.map.assign(reinterpret_cast<const char*>(p+48),length);std::memcpy(result.build.data(),p+112,32);
    if(!result.pid||!result.sequence||!result.session||!valid_map(result.map))return {};
    for(unsigned i=0;i<3;++i){result.position[i]=f64(p+144+i*8);result.forward[i]=f64(p+168+i*8);}
    for(double v:result.position)if(!std::isfinite(v)||std::abs(v)>20000)return {};
    double norm=0;for(double v:result.forward){if(!std::isfinite(v))return {};norm+=v*v;}
    if(result.available&&(norm<.98||norm>1.02||std::hypot(result.forward[0],result.forward[2])<.01))return {};
    return result;
 }catch(...){return {};}
}
SrmmCandidateGate::SrmmCandidateGate(std::uint32_t pid,std::uint64_t session,std::array<std::uint8_t,32> build,std::string map,SrmmCalibration calibration,bool alignment_preview)
 :pid_(pid),session_(session),build_(build),map_(std::move(map)),calibration_(calibration),alignment_preview_(alignment_preview){}
SrmmCandidateFrame SrmmCandidateGate::consume(const void* data,std::size_t size,std::uint64_t now) noexcept {
    auto hide=[](SrmmPacketStatus status){return SrmmCandidateFrame{status,{}};};
    if(poisoned_||(have_tick_&&now<last_tick_)){poisoned_=true;return hide(SrmmPacketStatus::clock_error);}
    have_tick_=true;last_tick_=now;
    const auto packet=srmm_parse_candidate(data,size);if(!packet)return hide(SrmmPacketStatus::invalid);
    const auto& p=*packet;
    if(p.pid!=pid_||p.session!=session_||p.build!=build_||p.map!=map_)return hide(SrmmPacketStatus::wrong_context);
    if(p.sampled_ms>now||now-p.sampled_ms>750)return hide(SrmmPacketStatus::stale);
    if(have_sequence_&&(p.sequence<last_sequence_||p.sampled_ms<last_sampled_||(p.sequence==last_sequence_&&std::memcmp(data,accepted_.data(),256))))return hide(SrmmPacketStatus::out_of_order);
    last_sequence_=p.sequence;last_sampled_=p.sampled_ms;have_sequence_=true;std::memcpy(accepted_.data(),data,256);
    if(!p.available)return hide(SrmmPacketStatus::unavailable);
    if(!calibration_.validated&&!alignment_preview_)return hide(SrmmPacketStatus::unvalidated);
    const auto& a=calibration_.affine;
    for(auto v:a)if(!std::isfinite(v))return hide(SrmmPacketStatus::invalid);
    const auto vstep=calibration_.uv_per_pixel_v==0?calibration_.uv_per_pixel:calibration_.uv_per_pixel_v;
    if(!std::isfinite(vstep)||vstep<=0||vstep>1)return hide(SrmmPacketStatus::invalid);
    const auto determinant=a[0]*a[4]-a[1]*a[3];
    if(!std::isfinite(determinant)||!std::isfinite(calibration_.uv_per_pixel)||calibration_.uv_per_pixel<=0||calibration_.uv_per_pixel>1||std::abs(determinant)<1e-14)return hide(SrmmPacketStatus::invalid);
    const double u=a[0]*p.position[0]+a[1]*p.position[2]+a[2],v=a[3]*p.position[0]+a[4]*p.position[2]+a[5];
    const double du=a[0]*p.forward[0]+a[1]*p.forward[2],dv=a[3]*p.forward[0]+a[4]*p.forward[2];
    if(!std::isfinite(du)||!std::isfinite(dv)||!std::isfinite(u)||!std::isfinite(v)||u<0||u>1||v<0||v>1||std::hypot(du,dv)<1e-12)return hide(SrmmPacketStatus::invalid);
    SrmmMapFrame frame;frame.center_u=static_cast<float>(u);frame.center_v=static_cast<float>(v);frame.heading=static_cast<float>(std::atan2(du/calibration_.uv_per_pixel,-dv/vstep));
    frame.uv_per_pixel=calibration_.uv_per_pixel;frame.uv_per_pixel_v=vstep;frame.texture_v_flipped=calibration_.texture_v_flipped;
    frame.alignment_preview=!calibration_.validated;
    return {calibration_.validated?SrmmPacketStatus::ready_diagnostic:SrmmPacketStatus::ready_alignment_preview,frame};
}
