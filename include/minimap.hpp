#pragma once
#include <array>

namespace minimap {
struct Point { double x; double y; };
// Internal world convention: x = east, y = north; units supplied by adapter.
// Image convention: x = right, y = down. No SnowRunner axis claim is made.
struct Landmark { Point world; Point image; };
struct Calibration {
    double a, b, c, d, tx, ty;
    Point project(Point world) const;
    Point direction(Point world_vector) const;
    void validate() const;
};
Calibration calibrate(const std::array<Landmark, 3>& landmarks);
struct View {
    Point center;
    double pixels_per_image_pixel;
    double size;
    // Clockwise radians from image up, after projecting the world heading.
    double heading;
    bool heading_up;
    Point project(Point image_point) const;
};
double image_heading(const Calibration& calibration, double world_heading);
enum class Signal { unavailable, fresh, stale };
// Monotonic milliseconds from one clock domain; future samples are invalid.
Signal signal_state(bool has_sample, double sampled_ms, double now_ms, double stale_after_ms);
bool inside_circle(Point screen_point, double size);
} // namespace minimap
