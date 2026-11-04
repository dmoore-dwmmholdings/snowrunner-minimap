#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

enum class SrmmUiState : std::uint32_t { unknown=0,gameplay=1,menu=2 };
struct SrmmUiStatePacket {
    SrmmUiState state=SrmmUiState::unknown;
    std::uint32_t pid{};
    std::uint64_t sequence{},sampled_ms{},session{};
    std::array<std::uint8_t,32> build{};
};
std::optional<SrmmUiStatePacket> srmm_parse_ui_state(const void*,std::size_t) noexcept;
enum class SrmmUiPacketStatus { ready,invalid,wrong_context,stale,out_of_order,clock_error };
struct SrmmUiStateResult { SrmmUiPacketStatus status=SrmmUiPacketStatus::invalid;SrmmUiState state=SrmmUiState::unknown; };
// Pure context/freshness gate: no process access, I/O or guessed menu semantics.
class SrmmUiStateGate {
    std::uint32_t pid_;
    std::uint64_t session_,last_sequence_{},last_sampled_{},last_now_{};
    std::array<std::uint8_t,32> build_;
    std::array<std::uint8_t,128> accepted_{};
    bool have_packet_=false,have_now_=false,poisoned_=false;
public:
    SrmmUiStateGate(std::uint32_t pid,std::uint64_t session,std::array<std::uint8_t,32> build):pid_(pid),session_(session),build_(build){}
    SrmmUiStateResult consume(const void*,std::size_t,std::uint64_t now_ms) noexcept;
};
