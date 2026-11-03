#pragma once
#include "candidate_packet.h"
#include "file_ingestor.h"
#include <thread>
enum class SrmmViewStatus { hidden,unavailable,wrong_generation,wrong_thread,graphics_error,ready_diagnostic };
struct SrmmViewResult { SrmmViewStatus status;SrmmPacketStatus packet_status=SrmmPacketStatus::invalid;HRESULT graphics=S_OK; };
// All methods/destruction belong to the render thread. No files, waits or worker mutation here.
class SrmmDiagnosticView {
    const std::thread::id owner_=std::this_thread::get_id();
    const std::uint64_t generation_;
    const std::array<std::uint8_t,32> asset_hash_;
    SrmmCandidateGate gate_;
    SharedRenderer renderer_;
    std::shared_ptr<const SrmmMapAsset> asset_;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    void clear() noexcept;
public:
    SrmmDiagnosticView(std::uint64_t generation,std::array<std::uint8_t,32> asset_hash,
        std::uint32_t pid,std::uint64_t session,std::array<std::uint8_t,32> build,std::string map,SrmmCalibration calibration);
    SrmmViewResult draw(IDXGISwapChain*,const std::shared_ptr<const SrmmIngestSnapshot>&,std::uint64_t now_ms,bool hud_allowed) noexcept;
};
