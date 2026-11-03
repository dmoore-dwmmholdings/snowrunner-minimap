#include "minimap.hpp"
#include <cmath>
#include <stdexcept>

namespace minimap {
namespace {
bool finite(Point p) { return std::isfinite(p.x) && std::isfinite(p.y); }
void require(bool ok, const char* message) { if (!ok) throw std::invalid_argument(message); }
}
void Calibration::validate() const {
    require(std::isfinite(a) && std::isfinite(b) && std::isfinite(c) &&
            std::isfinite(d) && std::isfinite(tx) && std::isfinite(ty), "nonfinite calibration");
    const double det = a * d - b * c;
    require(std::isfinite(det) && std::abs(det) > 1e-12, "singular calibration");
}
Point Calibration::project(Point world) const {
    validate();
    require(finite(world), "nonfinite world point");
    Point p{a * world.x + b * world.y + tx, c * world.x + d * world.y + ty};
    require(finite(p), "projection overflow");
    return p;
}
Point Calibration::direction(Point vector) const {
    validate();
    require(finite(vector), "nonfinite direction");
    Point p{a * vector.x + b * vector.y, c * vector.x + d * vector.y};
    require(finite(p), "direction overflow");
    return p;
}
Calibration calibrate(const std::array<Landmark, 3>& p) {
    for (const auto& landmark : p)
        require(finite(landmark.world) && finite(landmark.image), "nonfinite landmark");
    const double x1=p[1].world.x-p[0].world.x, y1=p[1].world.y-p[0].world.y;
    const double x2=p[2].world.x-p[0].world.x, y2=p[2].world.y-p[0].world.y;
    const double det=x1*y2-x2*y1;
    require(std::isfinite(det) && std::abs(det)>1e-12, "collinear world landmarks");
    const double u1=p[1].image.x-p[0].image.x, v1=p[1].image.y-p[0].image.y;
    const double u2=p[2].image.x-p[0].image.x, v2=p[2].image.y-p[0].image.y;
    Calibration result{(u1*y2-u2*y1)/det, (x1*u2-x2*u1)/det,
                       (v1*y2-v2*y1)/det, (x1*v2-x2*v1)/det, 0, 0};
    result.tx=p[0].image.x-result.a*p[0].world.x-result.b*p[0].world.y;
    result.ty=p[0].image.y-result.c*p[0].world.x-result.d*p[0].world.y;
    result.validate();
    return result;
}
double image_heading(const Calibration& calibration, double world_heading) {
    require(std::isfinite(world_heading), "nonfinite heading");
    const auto forward=calibration.direction({std::sin(world_heading), std::cos(world_heading)});
    return std::atan2(forward.x, -forward.y);
}
Point View::project(Point point) const {
    require(finite(center) && finite(point) && std::isfinite(size) && size>0 &&
            std::isfinite(pixels_per_image_pixel) && pixels_per_image_pixel>0 &&
            std::isfinite(heading), "invalid viewport");
    const double angle=heading_up ? heading : 0;
    const double dx=(point.x-center.x)*pixels_per_image_pixel;
    const double dy=(point.y-center.y)*pixels_per_image_pixel;
    Point result{size/2+std::cos(angle)*dx+std::sin(angle)*dy,
                 size/2-std::sin(angle)*dx+std::cos(angle)*dy};
    require(finite(result), "viewport overflow");
    return result;
}
Signal signal_state(bool has_sample, double sampled_ms, double now_ms, double stale_after_ms) {
    require(std::isfinite(stale_after_ms) && stale_after_ms>0, "invalid stale threshold");
    if (!has_sample || !std::isfinite(sampled_ms) || !std::isfinite(now_ms) ||
        sampled_ms<0 || now_ms<sampled_ms) return Signal::unavailable;
    return now_ms-sampled_ms>=stale_after_ms ? Signal::stale : Signal::fresh;
}
bool inside_circle(Point p, double size) {
    require(finite(p) && std::isfinite(size) && size>0, "invalid circle");
    return std::hypot(p.x-size/2, p.y-size/2)<=size/2;
}
} // namespace minimap
