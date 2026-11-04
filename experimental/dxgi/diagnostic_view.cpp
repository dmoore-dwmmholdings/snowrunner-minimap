#include "diagnostic_view.h"
#include <utility>
#include <cmath>
using Microsoft::WRL::ComPtr;
SrmmDiagnosticView::SrmmDiagnosticView(std::uint64_t generation,std::array<std::uint8_t,32> asset_hash,
    std::uint32_t pid,std::uint64_t session,std::array<std::uint8_t,32> build,std::string map,SrmmCalibration calibration,bool alignment_preview,float left,float top,float size)
 :generation_(generation),asset_hash_(asset_hash),gate_(pid,session,build,map,calibration,alignment_preview),destination_gate_(pid,session,build,std::move(map)),calibration_(calibration),left_(left),top_(top),size_(size){}
void SrmmDiagnosticView::clear() noexcept {renderer_.reset();asset_.reset();device_.Reset();}
SrmmViewResult SrmmDiagnosticView::draw(IDXGISwapChain* swap,const std::shared_ptr<const SrmmIngestSnapshot>& snapshot,std::uint64_t now,bool hud_allowed) noexcept {
    if(std::this_thread::get_id()!=owner_)return {SrmmViewStatus::wrong_thread};
    destinations_={};
    if(!snapshot||snapshot->generation!=generation_){clear();return {SrmmViewStatus::wrong_generation};}
    if(!snapshot->admission||!snapshot->admission->load(std::memory_order_acquire)||snapshot->status!=SrmmIngestStatus::ready||!snapshot->asset||snapshot->asset->sha256!=asset_hash_){clear();return {SrmmViewStatus::unavailable};}
    if(snapshot->have_destinations)destinations_=destination_gate_.consume(snapshot->destinations.data(),snapshot->destinations.size(),now);
    // Update saved-source status even when the HUD is suppressed by a menu.
    if(!hud_allowed){rotation_.reset();clear();return {SrmmViewStatus::hidden};}
    const auto frame=gate_.consume(snapshot->packet.data(),snapshot->packet.size(),now);
    if(!frame.frame){have_motion_=false;rotation_.reset();clear();return {SrmmViewStatus::unavailable,frame.status};}
    if(!swap){clear();return {SrmmViewStatus::graphics_error,frame.status,E_INVALIDARG};}
    ComPtr<ID3D11Device> device;auto hr=swap->GetDevice(IID_PPV_ARGS(&device));
    if(SUCCEEDED(hr)&&(asset_.get()!=snapshot->asset.get()||device_.Get()!=device.Get())) {
        clear();hr=renderer_.initialize_dds(device.Get(),snapshot->asset->dds.data(),snapshot->asset->dds.size());
        if(SUCCEEDED(hr)){asset_=snapshot->asset;device_=device;}
    }
    if(SUCCEEDED(hr)){auto placement=*frame.frame;placement.left=left_;placement.top=top_;placement.size=size_;placement.square=square_;placement.uv_per_pixel/=zoom_;placement.uv_per_pixel_v/=zoom_;
        const float du=(placement.center_u-last_u_)/placement.uv_per_pixel,dv=(placement.center_v-last_v_)/placement.uv_per_pixel_v;
        if(!have_motion_||now-motion_tick_>2000){motion_=placement.heading;last_u_=placement.center_u;last_v_=placement.center_v;have_motion_=true;motion_tick_=now;}
        else if(std::hypot(du,dv)>.02f){motion_=std::atan2(du,-dv);last_u_=placement.center_u;last_v_=placement.center_v;motion_tick_=now;}
        // Hold the last travel direction when stationary; reset only on missing feed.
        else motion_tick_=now;
        placement.map_rotation=rotation_.step(tracking_==2?motion_:tracking_==1?placement.heading:0,now);
        placement.heading-=placement.map_rotation;
        srmm_place_saved_waypoints(placement,destinations_,calibration_);
        hr=renderer_.draw(swap,placement);}
    if(FAILED(hr)){clear();return {SrmmViewStatus::graphics_error,frame.status,hr};}
    return {SrmmViewStatus::ready_diagnostic,frame.status,S_OK};
}
