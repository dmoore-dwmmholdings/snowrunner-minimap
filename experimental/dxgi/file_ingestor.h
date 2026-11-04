#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

enum class SrmmIngestStatus { starting,ready,missing_packet,invalid_packet,asset_error,io_error,stop_requested,stopped };
struct SrmmMapAsset { std::vector<std::uint8_t> dds;std::array<std::uint8_t,32> sha256{}; };
struct SrmmIngestSnapshot {
    SrmmIngestStatus status=SrmmIngestStatus::starting;
    std::uint64_t generation{},revision{};
    std::shared_ptr<const SrmmMapAsset> asset;
    std::shared_ptr<const std::atomic<bool>> admission;
    std::array<std::uint8_t,256> packet{};
    std::array<std::uint8_t,672> destinations{};
    bool have_destinations=false;
    std::array<std::uint8_t,128> ui_state{};
    bool have_ui_state=false;
};
struct SrmmIngestConfig {
    std::wstring map_path,packet_path,destination_path,ui_state_path; // Empty destination path disables saved markers only.
    std::array<std::uint8_t,32> map_sha256{};
    std::uint32_t poll_ms=100,read_timeout_ms=500;
};
bool srmm_sha256(const void*,std::size_t,std::array<std::uint8_t,32>&) noexcept;
// start/stop/wait/destruction belong to one control thread, outside DllMain/render callbacks.
// snapshot() may be called concurrently. Each object is single-start; reconfiguration creates a new generation.
class SrmmFileIngestor {
    struct State;
    std::shared_ptr<State> state_;
    void* thread_{};
    bool attempted_=false;
    static unsigned long __stdcall worker(void*) noexcept;
public:
    explicit SrmmFileIngestor(SrmmIngestConfig);
    ~SrmmFileIngestor();
    SrmmFileIngestor(const SrmmFileIngestor&)=delete;
    SrmmFileIngestor& operator=(const SrmmFileIngestor&)=delete;
    bool start() noexcept;
    void request_stop() noexcept;
    bool wait_for_stop(std::uint32_t timeout_ms) noexcept; // false means pending, never forced termination.
    std::shared_ptr<const SrmmIngestSnapshot> snapshot() const noexcept;
    std::uint64_t generation() const noexcept;
};
