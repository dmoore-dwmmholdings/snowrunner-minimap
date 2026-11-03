#include "map_dds.h"
#include <cstring>
namespace { std::uint32_t word(const std::uint8_t* p){return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);} }
std::optional<SrmmDdsView> srmm_parse_map_dds(const void* data,std::size_t size) noexcept {
    if(!data||size<144||size>128u+8192u*8192u)return {};
    const auto* p=static_cast<const std::uint8_t*>(data);
    if(std::memcmp(p,"DDS ",4)||word(p+4)!=124||word(p+8)!=0x81007||word(p+24)!=0||word(p+28)!=1||
       word(p+76)!=32||word(p+80)!=4||word(p+84)!=0x35545844||word(p+108)!=0x1000)return {};
    for(unsigned i=32;i<76;i+=4)if(word(p+i))return {};
    for(unsigned i=88;i<108;i+=4)if(word(p+i))return {};
    for(unsigned i=112;i<128;i+=4)if(word(p+i))return {};
    const auto width=word(p+16),height=word(p+12);
    if(width<4||height<4||width>8192||height>8192||width%4||height%4)return {};
    const auto bytes=std::size_t(width)*height;
    if(word(p+20)!=bytes||size!=128+bytes)return {};
    return SrmmDdsView{width,height,width*4,p+128,bytes};
}
