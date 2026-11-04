#pragma once
#include "candidate_packet.h"
#include "file_ingestor.h"
#include <memory>

// Explicit private diagnostic session; no deployment or process-memory access.
struct SrmmSessionConfig {
    SrmmIngestConfig files;
    bool enabled=true;
    std::uint64_t session{};
    std::string map;
    SrmmCalibration calibration;
    float left=32,top=32,size=320,zoom=1;
    int tracking=2;bool square=false;
};
bool srmm_same_map_binding(const SrmmSessionConfig&,const SrmmSessionConfig&) noexcept;
bool srmm_parse_session(const std::string&,SrmmSessionConfig&) noexcept;

struct SrmmSessionHealth {
    bool configured=false,enabled=false,admitted=false;
    std::uint64_t generation=0;
    SrmmIngestStatus status=SrmmIngestStatus::starting;
    std::string map;
    float zoom=1;
};
class SrmmGameLayer {
    struct State;
    struct View;
    std::shared_ptr<State> state_;
    std::unique_ptr<View> view_;
    static unsigned long __stdcall worker(void*) noexcept;
public:
    SrmmGameLayer();
    ~SrmmGameLayer();
    void draw(IDXGISwapChain*,unsigned flags) noexcept;
    SrmmSessionHealth health() const noexcept;
};
