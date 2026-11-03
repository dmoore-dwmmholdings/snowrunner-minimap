#include "diagnostic_view.h"
#include <utility>
using Microsoft::WRL::ComPtr;
SrmmDiagnosticView::SrmmDiagnosticView(std::uint64_t generation,std::array<std::uint8_t,32> asset_hash,
    std::uint32_t pid,std::uint64_t session,std::array<std::uint8_t,32> build,std::string map,SrmmCalibration calibration)
 :generation_(generation),asset_hash_(asset_hash),gate_(pid,session,build,std::move(map),calibration){}
void SrmmDiagnosticView::clear() noexcept {renderer_.reset();asset_.reset();device_.Reset();}
SrmmViewResult SrmmDiagnosticView::draw(IDXGISwapChain* swap,const std::shared_ptr<const SrmmIngestSnapshot>& snapshot,std::uint64_t now,bool hud_allowed) noexcept {
    if(std::this_thread::get_id()!=owner_)return {SrmmViewStatus::wrong_thread};
    if(!hud_allowed){clear();return {SrmmViewStatus::hidden};}
    if(!snapshot||snapshot->generation!=generation_){clear();return {SrmmViewStatus::wrong_generation};}
    if(!snapshot->admission||!snapshot->admission->load(std::memory_order_acquire)||snapshot->status!=SrmmIngestStatus::ready||!snapshot->asset||snapshot->asset->sha256!=asset_hash_){clear();return {SrmmViewStatus::unavailable};}
    const auto frame=gate_.consume(snapshot->packet.data(),snapshot->packet.size(),now);
    if(!frame.frame){clear();return {SrmmViewStatus::unavailable,frame.status};}
    if(!swap){clear();return {SrmmViewStatus::graphics_error,frame.status,E_INVALIDARG};}
    ComPtr<ID3D11Device> device;auto hr=swap->GetDevice(IID_PPV_ARGS(&device));
    if(SUCCEEDED(hr)&&(asset_.get()!=snapshot->asset.get()||device_.Get()!=device.Get())) {
        clear();hr=renderer_.initialize_dds(device.Get(),snapshot->asset->dds.data(),snapshot->asset->dds.size());
        if(SUCCEEDED(hr)){asset_=snapshot->asset;device_=device;}
    }
    if(SUCCEEDED(hr))hr=renderer_.draw(swap,*frame.frame);
    if(FAILED(hr)){clear();return {SrmmViewStatus::graphics_error,frame.status,hr};}
    return {SrmmViewStatus::ready_diagnostic,frame.status,S_OK};
}
