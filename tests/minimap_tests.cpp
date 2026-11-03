#include "minimap.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
using namespace minimap;
int checks=0;
void check(bool ok, const std::string& name) {
    ++checks;
    if (!ok) throw std::runtime_error(name);
}
void near(Point p, Point expected, const std::string& name) {
    check(std::abs(p.x-expected.x)<1e-8 && std::abs(p.y-expected.y)<1e-8, name);
}
template<class F> void rejects(F action, const std::string& name) {
    bool threw=false;
    try { action(); } catch(const std::invalid_argument&) { threw=true; }
    check(threw, name);
}
int main() {
    try {
        // Independent measured landmarks include translation and north/y flip.
        auto c=calibrate({Landmark{{-100,-100},{0,400}},
                          Landmark{{100,-100},{400,400}},
                          Landmark{{-100,100},{0,0}}});
        near(c.project({0,0}), {200,200}, "center landmark");
        near(c.project({50,75}), {300,50}, "held-out landmark");
        near(c.project({-100,100}), {0,0}, "image origin");
        // A rotated/sheared image: affine calibration cannot assume north-up.
        auto rotated=calibrate({Landmark{{0,0},{100,200}},
                                Landmark{{10,0},{100,180}},
                                Landmark{{0,10},{130,210}}});
        near(rotated.project({5,5}), {115,195}, "sheared held-out point");
        constexpr double pi=3.14159265358979323846;
        for (int i=0;i<4;++i) {
            double h=i*pi/2;
            View view{{200,200}, 1, 300, image_heading(c,h), true};
            near(view.project(c.project({0,0})), {150,150}, "player centered");
            near(view.project(c.project({10*std::sin(h),10*std::cos(h)})),
                 {150,130}, "heading-up cardinal forward");
        }
        View north{{200,200}, 2, 300, pi/2, false};
        near(north.project({210,200}), {170,150}, "north-up ignores heading and scales");
        View rotated_view{rotated.project({0,0}), 1, 300, image_heading(rotated,0), true};
        auto forward=rotated_view.project(rotated.project({0,1}));
        check(std::abs(forward.x-150)<1e-8 && forward.y<150, "calibrated image heading");
        check(inside_circle({150,0},300), "circle boundary");
        check(!inside_circle({0,0},300), "circle excludes corner");
        check(signal_state(false,0,0,1000)==Signal::unavailable,"no data");
        check(signal_state(true,0,999,1000)==Signal::fresh,"fresh before boundary");
        check(signal_state(true,0,1000,1000)==Signal::stale,"stale at boundary");
        check(signal_state(true,1001,1000,1000)==Signal::unavailable,"future timestamp");
        double nan=std::numeric_limits<double>::quiet_NaN();
        check(signal_state(true,nan,1000,1000)==Signal::unavailable,"invalid timestamp");
        rejects([&]{calibrate({Landmark{{0,0},{0,0}},Landmark{{1,1},{1,1}},Landmark{{2,2},{2,2}}});},"collinear landmarks");
        rejects([&]{Calibration{1,2,2,4,0,0}.validate();},"singular image transform");
        rejects([&]{c.project({nan,0});},"nonfinite position");
        rejects([&]{image_heading(c,nan);},"nonfinite heading");
        rejects([&]{View{{0,0},0,100,0,false}.project({1,1});},"zero zoom");
        rejects([&]{signal_state(true,0,0,0);},"zero stale threshold");
        std::cout<<checks<<" checks passed\n";
    } catch(const std::exception& e) {
        std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';
        return 1;
    }
}
