#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

inline float srmm_wrap_angle(float a) noexcept {return std::remainder(a,6.28318530718f);}
class SrmmBearingFilter {
    bool ready_=false;float value_=0;std::uint64_t tick_=0;
public:
    void reset() noexcept {ready_=false;}
    float step(float target,std::uint64_t now) noexcept {
        if(!std::isfinite(target))return value_;
        if(!ready_||now<tick_||now-tick_>750){value_=target;ready_=true;}
        else {const float dt=static_cast<float>(now-tick_)*.001f;
            value_=srmm_wrap_angle(value_+srmm_wrap_angle(target-value_)*(1-std::exp(-dt/.06f)));}
        tick_=now;return value_;
    }
};
struct SrmmDestinationProjection {float x,y,bearing;bool edge;};
// Pure projection only: callers must first validate source identity/freshness.
inline std::optional<SrmmDestinationProjection> srmm_project_destination(float du,float dv,float scale,float rotation,float size,bool square,float padding=12,float scale_v=0) noexcept {
    if(scale_v==0)scale_v=scale;
    if(!std::isfinite(scale_v)||scale_v<=0)return {};
    if(!std::isfinite(du)||!std::isfinite(dv)||!std::isfinite(scale)||!std::isfinite(rotation)||!std::isfinite(size)||!std::isfinite(padding)||scale<=0||size<32||padding<0||padding>=size/2)return {};
    const float c=std::cos(rotation),s=std::sin(rotation);
    const float px=du/scale,py=dv/scale_v;
    float x=c*px+s*py,y=-s*px+c*py;
    if(!std::isfinite(x)||!std::isfinite(y))return {};
    const float extent=square?(std::max)(std::abs(x),std::abs(y)):std::hypot(x,y);
    const float limit=size/2-padding;const bool edge=extent>limit;
    const float bearing=std::atan2(x,-y);
    if(edge){x*=limit/extent;y*=limit/extent;}
    return SrmmDestinationProjection{x,y,bearing,edge};
}
