#pragma once
#include "shared_renderer.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// Diagnostic wire contract; no process access, clocks or filesystem I/O here.
struct SrmmCandidatePacket {
    std::uint32_t pid{};
    std::uint64_t sequence{},sampled_ms{},session{};
    bool available{};
    std::string map;
    std::array<std::uint8_t,32> build{};
    std::array<double,3> position{},forward{};
};
std::optional<SrmmCandidatePacket> srmm_parse_candidate(const void*,std::size_t) noexcept;
struct SrmmCalibration {
    // North-up UV = affine(X,Z). All six values must be measured, never inferred from image size.
    std::array<double,6> affine{};
    float uv_per_pixel=.001f;
    bool texture_v_flipped=false;
    bool validated=false;
};
enum class SrmmPacketStatus { ready_diagnostic,ready_alignment_preview,unvalidated,invalid,wrong_context,unavailable,stale,out_of_order,clock_error };
struct SrmmCandidateFrame { SrmmPacketStatus status=SrmmPacketStatus::invalid;std::optional<SrmmMapFrame> frame; };
class SrmmCandidateGate {
    std::uint32_t pid_;
    std::uint64_t session_,last_sequence_{},last_tick_{},last_sampled_{};
    std::array<std::uint8_t,32> build_;
    std::string map_;
    SrmmCalibration calibration_;
    std::array<std::uint8_t,256> accepted_{};
    bool alignment_preview_=false;
    bool have_sequence_=false,have_tick_=false,poisoned_=false;
public:
    SrmmCandidateGate(std::uint32_t pid,std::uint64_t session,std::array<std::uint8_t,32> build,std::string map,SrmmCalibration calibration,bool alignment_preview=false);
    // Diagnostic replay only: even validated geometry is not promoted into the production native source.
    SrmmCandidateFrame consume(const void*,std::size_t,std::uint64_t now_ms) noexcept;
};
