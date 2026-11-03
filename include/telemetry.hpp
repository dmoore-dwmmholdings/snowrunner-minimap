#pragma once
#include "minimap.hpp"
#include <cstdint>
#include <optional>
#include <string>

namespace minimap {
// An exact manifest identity, not an automatic assertion of game compatibility.
struct BuildIdentity {
    std::string storefront, version, executable_sha256, architecture, graphics_api;
    bool valid() const;
    bool operator==(const BuildIdentity& other) const;
};
enum class SourceKind { synthetic, native_candidate };
enum class ReadState { active, menu, garage, loading, disconnected, read_error };
struct ContextKey {
    std::string session, map, vehicle;
    bool valid() const;
    bool operator==(const ContextKey& other) const;
};
struct Pose {
    Point world; // Adapter must convert to east/north before this boundary.
    double heading; // Clockwise radians from world north, not camera heading.
};
struct TelemetrySample {
    SourceKind source=SourceKind::synthetic;
    ReadState state=ReadState::disconnected;
    BuildIdentity build;
    ContextKey context;
    std::uint64_t sequence=0; // Strictly increasing within one session/context.
    double sampled_ms=0; // Monotonic clock, same domain as FrameController::update.
    std::optional<Pose> pose;
};
class TelemetrySource {
public:
    virtual ~TelemetrySource()=default;
    // nullopt means no new event; failures/transitions must be explicit events.
    // No native implementation, process handle or Windows API is included.
    virtual std::optional<TelemetrySample> poll()=0;
};
struct MapContext {
    ContextKey key;
    Calibration calibration;
    double viewport_size=220;
    double zoom=1;
    bool heading_up=true;
};
enum class FrameState {
    synthetic_ready, no_pose, stale, clock_error, invalid_context,
    unsupported_source, wrong_build, wrong_context, out_of_order,
    invalid_pose, menu, garage, loading, disconnected, read_error
};
const char* frame_status(FrameState state);
struct FrameGeometry {
    View view;
    Point player;
    double marker_heading;
    ContextKey context;
};
struct RenderPacket {
    FrameState state;
    std::optional<FrameGeometry> geometry;
};
// Single-threaded core: a future native worker must hand off immutable snapshots.
// Native candidates always fail closed in this prototype, even on a matching hash.
class FrameController {
public:
    explicit FrameController(BuildIdentity expected, double stale_after_ms=1000);
    RenderPacket update(TelemetrySource& source, const MapContext& context, double now_ms);
private:
    BuildIdentity expected_;
    double stale_after_ms_;
    std::optional<double> last_now_;
    std::optional<ContextKey> context_;
    std::optional<TelemetrySample> cached_;
    std::uint64_t last_sequence_=0;
    FrameState empty_state_=FrameState::no_pose;
    RenderPacket clear(FrameState state);
};
} // namespace minimap
