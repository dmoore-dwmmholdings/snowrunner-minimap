#pragma once
#include "candidate_packet.h"
#include "file_ingestor.h"
#include <memory>

// Explicit private diagnostic session; no deployment or process-memory access.
struct SrmmSessionConfig {
    SrmmIngestConfig files;
    std::uint64_t session{};
    std::string map;
    SrmmCalibration calibration;
    float left=32,top=32,size=320;
};
bool srmm_parse_session(const std::string&,SrmmSessionConfig&) noexcept;

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
};
