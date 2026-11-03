#include "telemetry.hpp"
#include <cmath>
#include <stdexcept>
#include <utility>

namespace minimap {
bool BuildIdentity::valid() const {
    if (storefront.empty() || version.empty() || architecture.empty() || graphics_api.empty() ||
        executable_sha256.size()!=64) return false;
    for (char c : executable_sha256)
        if (!((c>='0' && c<='9') || (c>='a' && c<='f'))) return false;
    return true;
}
bool BuildIdentity::operator==(const BuildIdentity& o) const {
    return storefront==o.storefront && version==o.version && executable_sha256==o.executable_sha256 &&
           architecture==o.architecture && graphics_api==o.graphics_api;
}
bool ContextKey::valid() const { return !session.empty() && !map.empty() && !vehicle.empty(); }
bool ContextKey::operator==(const ContextKey& o) const {
    return session==o.session && map==o.map && vehicle==o.vehicle;
}
const char* frame_status(FrameState state) {
    switch(state) {
    case FrameState::synthetic_ready: return "SYNTHETIC / no game connection";
    case FrameState::no_pose: return "No pose";
    case FrameState::stale: return "Stale pose / marker hidden";
    case FrameState::clock_error: return "Invalid monotonic clock";
    case FrameState::invalid_context: return "Invalid map context";
    case FrameState::unsupported_source: return "Native integration unavailable";
    case FrameState::wrong_build: return "Build identity mismatch";
    case FrameState::wrong_context: return "Session, map or vehicle mismatch";
    case FrameState::out_of_order: return "Repeated or out-of-order sample";
    case FrameState::invalid_pose: return "Invalid pose";
    case FrameState::menu: return "Menu / marker hidden";
    case FrameState::garage: return "Garage / marker hidden";
    case FrameState::loading: return "Loading / marker hidden";
    case FrameState::disconnected: return "Disconnected / marker hidden";
    case FrameState::read_error: return "Read error / marker hidden";
    }
    return "Unknown state / marker hidden";
}
FrameController::FrameController(BuildIdentity expected, double stale_after_ms)
    : expected_(std::move(expected)), stale_after_ms_(stale_after_ms) {
    if (!expected_.valid() || !std::isfinite(stale_after_ms_) || stale_after_ms_<=0)
        throw std::invalid_argument("invalid adapter configuration");
}
RenderPacket FrameController::clear(FrameState state) {
    cached_.reset();
    empty_state_=state;
    return {state, std::nullopt};
}
RenderPacket FrameController::update(TelemetrySource& source, const MapContext& context, double now_ms) {
    if (!std::isfinite(now_ms) || now_ms<0 || (last_now_ && now_ms<*last_now_))
        return clear(FrameState::clock_error);
    last_now_=now_ms;
    if (!context.key.valid() || !std::isfinite(context.viewport_size) || context.viewport_size<=0 ||
        !std::isfinite(context.zoom) || context.zoom<=0) return clear(FrameState::invalid_context);
    try { context.calibration.validate(); }
    catch(const std::invalid_argument&) { return clear(FrameState::invalid_context); }
    if (!context_ || !(*context_==context.key)) {
        clear(FrameState::no_pose);
        last_sequence_=0;
        context_=context.key;
    }
    std::optional<TelemetrySample> event;
    try { event=source.poll(); }
    catch(...) { return clear(FrameState::read_error); }
    if (event) {
        clear(FrameState::no_pose); // Every rejected event erases the old marker.
        if (event->source!=SourceKind::synthetic) return clear(FrameState::unsupported_source);
        switch(event->state) {
        case ReadState::menu: return clear(FrameState::menu);
        case ReadState::garage: return clear(FrameState::garage);
        case ReadState::loading: return clear(FrameState::loading);
        case ReadState::disconnected: return clear(FrameState::disconnected);
        case ReadState::read_error: return clear(FrameState::read_error);
        case ReadState::active: break;
        default: return clear(FrameState::read_error);
        }
        if (!event->build.valid() || !(event->build==expected_)) return clear(FrameState::wrong_build);
        if (!event->context.valid() || !(event->context==context.key)) return clear(FrameState::wrong_context);
        if (event->sequence==0 || event->sequence<=last_sequence_) return clear(FrameState::out_of_order);
        last_sequence_=event->sequence;
        if (!event->pose || !std::isfinite(event->pose->world.x) ||
            !std::isfinite(event->pose->world.y) || !std::isfinite(event->pose->heading))
            return clear(FrameState::invalid_pose);
        const auto signal=signal_state(true,event->sampled_ms,now_ms,stale_after_ms_);
        if (signal==Signal::unavailable) return clear(FrameState::invalid_pose);
        if (signal==Signal::stale) return clear(FrameState::stale);
        cached_=std::move(event);
    }
    if (!cached_) return {empty_state_,std::nullopt};
    if (signal_state(true,cached_->sampled_ms,now_ms,stale_after_ms_)!=Signal::fresh)
        return clear(FrameState::stale);
    try {
        const auto center=context.calibration.project(cached_->pose->world);
        const double heading=image_heading(context.calibration,cached_->pose->heading);
        View view{center,context.zoom,context.viewport_size,heading,context.heading_up};
        return {FrameState::synthetic_ready,
                FrameGeometry{view,view.project(center),context.heading_up ? 0 : heading,context.key}};
    } catch(const std::invalid_argument&) { return clear(FrameState::invalid_pose); }
}
} // namespace minimap
