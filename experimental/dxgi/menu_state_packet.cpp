#include "menu_state_packet.h"
#include <cstring>
namespace {
std::uint32_t u32(const std::uint8_t* p){return p[0]|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}
std::uint64_t u64(const std::uint8_t* p){return u32(p)|(std::uint64_t(u32(p+4))<<32);}
}
std::optional<SrmmUiStatePacket> srmm_parse_ui_state(const void* bytes,std::size_t size) noexcept {
    if(!bytes||size!=128)return {};
    const auto* p=static_cast<const std::uint8_t*>(bytes);
    if(std::memcmp(p,"SRMMUI01",8)||u32(p+8)!=1||u32(p+12)!=128||u32(p+16)>2)return {};
    for(unsigned i=80;i<128;++i)if(p[i])return {};
    SrmmUiStatePacket result;result.state=static_cast<SrmmUiState>(u32(p+16));result.pid=u32(p+20);
    result.sequence=u64(p+24);result.sampled_ms=u64(p+32);result.session=u64(p+40);
    std::memcpy(result.build.data(),p+48,32);
    if(!result.pid||!result.sequence||!result.session)return {};
    return result;
}
SrmmUiStateResult SrmmUiStateGate::consume(const void* bytes,std::size_t size,std::uint64_t now) noexcept {
    auto reject=[](SrmmUiPacketStatus status){return SrmmUiStateResult{status,SrmmUiState::unknown};};
    if(poisoned_||(have_now_&&now<last_now_)){poisoned_=true;return reject(SrmmUiPacketStatus::clock_error);}
    have_now_=true;last_now_=now;
    const auto packet=srmm_parse_ui_state(bytes,size);if(!packet)return reject(SrmmUiPacketStatus::invalid);
    const auto& p=*packet;
    if(p.pid!=pid_||p.session!=session_||p.build!=build_)return reject(SrmmUiPacketStatus::wrong_context);
    if(p.sampled_ms>now||now-p.sampled_ms>750)return reject(SrmmUiPacketStatus::stale);
    if(have_packet_&&(p.sequence<last_sequence_||p.sampled_ms<last_sampled_||(p.sequence==last_sequence_&&std::memcmp(bytes,accepted_.data(),128))))return reject(SrmmUiPacketStatus::out_of_order);
    last_sequence_=p.sequence;last_sampled_=p.sampled_ms;have_packet_=true;std::memcpy(accepted_.data(),bytes,128);
    return {SrmmUiPacketStatus::ready,p.state};
}
