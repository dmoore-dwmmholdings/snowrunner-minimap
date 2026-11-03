#include "diagnostic_api.h"
#include "telemetry.hpp"
#include <utility>

namespace {
minimap::BuildIdentity fixture_build() {
    return {"synthetic", "diagnostic-v1", std::string(64, 'a'), "synthetic", "none"};
}
struct Session {
    minimap::BuildIdentity build = fixture_build();
    minimap::MapContext context{{"diagnostic-session", "synthetic-map", "mock-truck"},
                               {1, 0, 0, -1, 250, 250}, 220, 1, true};
    minimap::FrameController controller{build, 1000};
    bool poisoned = false;
};
struct Source final : minimap::TelemetrySource {
    std::optional<minimap::TelemetrySample> event;
    std::optional<minimap::TelemetrySample> poll() override {
        return std::exchange(event, std::nullopt);
    }
};
uint32_t frame_state(minimap::FrameState state) noexcept {
    using F = minimap::FrameState;
    switch (state) {
    case F::synthetic_ready: return SRMM_READY;
    case F::no_pose: return SRMM_NO_POSE;
    case F::stale: return SRMM_STALE;
    case F::clock_error: return SRMM_CLOCK_ERROR;
    case F::invalid_context: return SRMM_INVALID_CONTEXT;
    case F::unsupported_source: return SRMM_UNSUPPORTED_SOURCE;
    case F::wrong_build: return SRMM_WRONG_BUILD;
    case F::wrong_context: return SRMM_WRONG_CONTEXT;
    case F::out_of_order: return SRMM_OUT_OF_ORDER;
    case F::invalid_pose: return SRMM_INVALID_POSE;
    case F::menu: return SRMM_FRAME_MENU;
    case F::garage: return SRMM_FRAME_GARAGE;
    case F::loading: return SRMM_FRAME_LOADING;
    case F::disconnected: return SRMM_FRAME_DISCONNECTED;
    case F::read_error: return SRMM_FRAME_READ_ERROR;
    }
    return SRMM_FRAME_READ_ERROR;
}
uint32_t SRMM_CALL create(SrmmSession* out) noexcept {
    if (!out) return SRMM_BAD_ARGUMENT;
    *out = nullptr;
    try {
        *out = new Session;
        return SRMM_OK;
    } catch (...) {
        return SRMM_INTERNAL_ERROR;
    }
}
uint32_t SRMM_CALL update(SrmmSession handle, const SrmmSample* sample, SrmmFrame* out) noexcept {
    auto* session = static_cast<Session*>(handle);
    const bool valid_out = out && out->size == sizeof(SrmmFrame);
    if (valid_out) *out = {sizeof(SrmmFrame), SRMM_FRAME_READ_ERROR, 0, 0, 0, 0, 0, 0, 0, 0};
    if (!session) return SRMM_BAD_ARGUMENT;
    if (session->poisoned) return SRMM_SESSION_POISONED;
    if (!valid_out || !sample || sample->size != sizeof(SrmmSample) ||
        sample->has_event > 1 || sample->source > SRMM_NATIVE_CANDIDATE ||
        sample->state > SRMM_READ_ERROR) {
        session->poisoned = true;
        return SRMM_BAD_ARGUMENT;
    }
    try {
        Source source;
        if (sample->has_event) {
            source.event = minimap::TelemetrySample{
                sample->source == SRMM_SYNTHETIC ? minimap::SourceKind::synthetic
                                                : minimap::SourceKind::native_candidate,
                static_cast<minimap::ReadState>(sample->state), session->build, session->context.key,
                sample->sequence, sample->sampled_ms,
                minimap::Pose{{sample->east, sample->north}, sample->heading_radians}};
        }
        auto packet = session->controller.update(source, session->context, sample->now_ms);
        out->state = frame_state(packet.state);
        if (packet.geometry) {
            const auto& geometry = *packet.geometry;
            out->image_center_x = geometry.view.center.x;
            out->image_center_y = geometry.view.center.y;
            out->player_x = geometry.player.x;
            out->player_y = geometry.player.y;
            out->view_heading = geometry.view.heading;
            out->marker_heading = geometry.marker_heading;
            out->has_geometry = 1;
        }
        return SRMM_OK;
    } catch (...) {
        session->poisoned = true;
        *out = {sizeof(SrmmFrame), SRMM_FRAME_READ_ERROR, 0, 0, 0, 0, 0, 0, 0, 0};
        return SRMM_INTERNAL_ERROR;
    }
}
void SRMM_CALL destroy(SrmmSession handle) noexcept {
    delete static_cast<Session*>(handle);
}
} // namespace

// No custom DllMain, global session, worker, callbacks, I/O or game access.
extern "C" __declspec(dllexport) uint32_t SRMM_CALL
srmm_get_api(uint32_t version, uint32_t size, SrmmApi* out) noexcept {
    if (!out || size != sizeof(SrmmApi)) return SRMM_BAD_ARGUMENT;
    *out = {};
    if (version != SRMM_DIAGNOSTIC_ABI) return SRMM_ABI_MISMATCH;
    *out = {sizeof(SrmmApi), SRMM_DIAGNOSTIC_ABI, SRMM_CAP_SYNTHETIC_ONLY, 0,
            &create, &update, &destroy};
    return SRMM_OK;
}
