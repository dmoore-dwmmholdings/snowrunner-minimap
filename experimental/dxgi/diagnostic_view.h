#pragma once
#include "candidate_packet.h"
#include "destination_packet.h"
#include "file_ingestor.h"
#include <thread>
#include "navigation_math.h"
enum class SrmmViewStatus { hidden,unavailable,wrong_generation,wrong_thread,graphics_error,ready_diagnostic };
struct SrmmViewResult { SrmmViewStatus status;SrmmPacketStatus packet_status=SrmmPacketStatus::invalid;HRESULT graphics=S_OK; };
// All methods/destruction belong to the render thread. No files, waits or worker mutation here.
class SrmmDiagnosticView {
    const std::thread::id owner_=std::this_thread::get_id();
    const std::uint64_t generation_;
    const std::array<std::uint8_t,32> asset_hash_;
    SrmmCandidateGate gate_;
    SrmmDestinationGate destination_gate_;
    const SrmmCalibration calibration_;
    SrmmDestinationFrame destinations_;
    float left_,top_,size_,zoom_=1;
    int tracking_=0;bool square_=false,have_motion_=false;
    SrmmBearingFilter rotation_;
    float motion_=0,last_u_=0,last_v_=0;std::uint64_t motion_tick_=0;
    SharedRenderer renderer_;
    std::shared_ptr<const SrmmMapAsset> asset_;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    void clear() noexcept;
public:
    SrmmDiagnosticView(std::uint64_t generation,std::array<std::uint8_t,32> asset_hash,
        std::uint32_t pid,std::uint64_t session,std::array<std::uint8_t,32> build,std::string map,SrmmCalibration calibration,bool alignment_preview=false,float left=16,float top=16,float size=96);
    void options(float left,float top,float size,float zoom,int tracking,bool square) noexcept {left_=left;top_=top;size_=size;zoom_=zoom;tracking_=tracking;square_=square;}
    std::optional<std::uint64_t> saved_modified_ms() const noexcept {return destinations_.status==SrmmDestinationStatus::saved_snapshot?std::optional<std::uint64_t>(destinations_.save_modified_unix_ms):std::nullopt;}
    std::uint32_t saved_destination_count() const noexcept {return destinations_.count;}
    SrmmDestinationStatus saved_destination_status() const noexcept {return destinations_.status;}
    SrmmViewResult draw(IDXGISwapChain*,const std::shared_ptr<const SrmmIngestSnapshot>&,std::uint64_t now_ms,bool hud_allowed) noexcept;
};
