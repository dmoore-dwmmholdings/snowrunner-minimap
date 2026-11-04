#pragma once
#include "candidate_packet.h"
#include <array>
#include <optional>

constexpr std::size_t SrmmDestinationPacketSize=672;
struct SrmmSavedWaypoint {std::uint32_t ordinal{};double x{},z{};std::uint32_t kind=1;};
struct SrmmDestinationPacket {
    std::uint32_t pid{},count{};
    std::uint64_t sequence{},sampled_ms{},session{},save_modified_unix_ms{};
    bool available{};
    std::string map;
    std::array<std::uint8_t,32> build{};
    std::array<SrmmSavedWaypoint,16> points{};
};
std::optional<SrmmDestinationPacket> srmm_parse_destinations(const void*,std::size_t) noexcept;
enum class SrmmDestinationStatus {saved_snapshot,invalid,wrong_context,unavailable,stale,out_of_order,clock_error};
struct SrmmDestinationFrame {
    SrmmDestinationStatus status=SrmmDestinationStatus::unavailable;
    std::uint64_t save_modified_unix_ms{};
    std::uint32_t count{};
    std::array<SrmmSavedWaypoint,16> points{};
};
class SrmmDestinationGate {
    std::uint32_t pid_;
    std::uint64_t session_,last_sequence_{},last_tick_{},last_sampled_{};
    std::array<std::uint8_t,32> build_;
    std::string map_;
    std::array<std::uint8_t,SrmmDestinationPacketSize> accepted_{};
    bool have_sequence_=false,have_tick_=false,poisoned_=false;
public:
    SrmmDestinationGate(std::uint32_t,std::uint64_t,std::array<std::uint8_t,32>,std::string);
    SrmmDestinationFrame consume(const void*,std::size_t,std::uint64_t now_ms) noexcept;
};
// Called only after the pose and destination gates admit the same map/session.
// Transport freshness does not turn these saved waypoints into live game data.
void srmm_place_saved_waypoints(SrmmMapFrame&,const SrmmDestinationFrame&,const SrmmCalibration&) noexcept;
