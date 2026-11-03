#include "mock_d3d11_renderer.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace minimap;
namespace {
struct Source : TelemetrySource {
    std::optional<TelemetrySample> event;
    std::optional<TelemetrySample> poll() override { auto result = event; event.reset(); return result; }
};
unsigned checks = 0;
void check(bool result, const char* description) {
    ++checks;
    if (!result) throw std::runtime_error(description);
}
bool rgb(const RgbaImage& image, unsigned x, unsigned y, std::array<unsigned char, 3> color) {
    const auto offset = (static_cast<size_t>(y)*image.size+x)*4;
    return std::equal(color.begin(), color.end(), image.pixels.begin()+offset) && image.pixels[offset+3] == 255;
}
bool empty(const RgbaImage& image) {
    return std::all_of(image.pixels.begin(), image.pixels.end(), [](auto value) { return value == 0; });
}
}
int main() {
    try {
        MockD3d11Renderer renderer;
        const BuildIdentity build{"synthetic", "d3d-host-v1", std::string(64,'a'), "x64", "WARP-owned"};
        MapContext context{{"render-fixture", "procedural-only", "mock-truck"}, {1,0,0,-1,250,250}, 256, 1, true};
        Source source;
        FrameController controller(build);
        std::uint64_t sequence = 0;
        double now = 0;
        auto frame = [&](Point position, double heading, ReadState state = ReadState::active) {
            now += 10;
            source.event = TelemetrySample{SourceKind::synthetic,state,build,context.key,++sequence,now,Pose{position,heading}};
            return controller.update(source, context, now);
        };
        const std::array<unsigned char,3> cyan{0,255,255}, white{255,255,255};
        auto packet = frame({0,0}, 0);
        auto north = renderer.render(packet);
        check(north.pixels.size() == 256*256*4, "Readback dimensions");
        check(north.pixels[3] == 0, "Circle clips corner alpha");
        check(rgb(north,128,128,white), "Player at center");
        check(rgb(north,178,128,cyan), "East landmark at expected pixel");
        constexpr double pi = 3.14159265358979323846;
        auto east = renderer.render(frame({0,0}, pi/2));
        check(rgb(east,128,78,cyan), "Heading-up rotates east landmark upward");
        check(!rgb(east,178,128,cyan), "Previous landmark pixels cleared");
        check(rgb(east,128,119,white), "Heading-up marker stays upright");
        context.zoom = 2;
        auto zoomed = renderer.render(frame({0,0}, 0));
        check(rgb(zoomed,228,128,cyan), "Zoom doubles landmark distance");
        context.zoom = 1;
        auto moved = renderer.render(frame({25,0}, 0));
        check(rgb(moved,153,128,cyan), "World movement shifts map under centered marker");
        context.heading_up = false;
        auto fixed = renderer.render(frame({0,0}, pi/2));
        check(rgb(fixed,178,128,cyan), "North-up map remains fixed");
        check(rgb(fixed,137,128,white), "North-up marker points east");
        check(!rgb(fixed,128,119,white), "North-up marker no longer points north");
        for (auto state : {ReadState::garage,ReadState::menu,ReadState::loading,ReadState::disconnected,ReadState::read_error})
            check(empty(renderer.render(frame({0,0},0,state))), "Transition clears entire texture");
        renderer.render(frame({0,0},0));
        check(empty(renderer.render(controller.update(source,context,now+1000))), "Stale sample clears texture");
        now += 1000;
        auto valid = frame({0,0},0);
        valid.geometry->view.heading = std::numeric_limits<double>::quiet_NaN();
        check(empty(renderer.render(valid)), "Nonfinite geometry hides output");
        valid = frame({0,0},0);
        valid.geometry->view.size = 220;
        check(empty(renderer.render(valid)), "Viewport mismatch hides output");
        valid = frame({0,0},0);
        valid.state = FrameState::unsupported_source;
        check(empty(renderer.render(valid)), "Unsupported state hides even supplied geometry");
        bool rejected = false;
        try { MockD3d11Renderer invalid(4096); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "Oversized allocation rejected");
        const auto old_packet = frame({0,0},0);
        for (unsigned dimension : {128u, 320u, 64u, 256u}) {
            renderer.resize(dimension);
            context.viewport_size = dimension;
            const auto resized = renderer.render(frame({0,0},0));
            check(resized.size == dimension && resized.pixels.size() == static_cast<size_t>(dimension)*dimension*4,
                  "Resize changes texture and readback dimensions together");
            check(rgb(resized,dimension/2,dimension/2,white), "Resize recenters marker");
            check(resized.pixels[3] == 0, "Resize preserves circular clipping");
            if (dimension >= 128)
                check(rgb(resized,dimension/2+50,dimension/2,cyan), "Resize preserves calibrated landmark distance");
            if (dimension != 256)
                check(empty(renderer.render(old_packet)), "Old viewport packet cannot draw after resize");
        }
        for (unsigned dimension : {1u, 63u, 2049u, std::numeric_limits<unsigned>::max()}) {
            rejected = false;
            try { renderer.resize(dimension); } catch (const std::invalid_argument&) { rejected = true; }
            check(rejected, "Invalid resize rejected before allocation");
            check(renderer.render(old_packet).pixels == north.pixels, "Rejected resize preserves previous renderer");
        }
        renderer.resize(0);
        auto suspended = renderer.render(old_packet);
        check(suspended.size == 0 && suspended.pixels.empty(), "Zero size suspends with no stale image");
        renderer.suspend();
        check(renderer.render(old_packet).pixels.empty(), "Repeated suspension is harmless");
        rejected = false;
        try { renderer.resize(63); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected && renderer.render(old_packet).size == 0, "Invalid resume remains suspended");
        renderer.resize(256);
        check(empty(renderer.render(frame({0,0},0,ReadState::menu))), "Recreated device starts with hidden menu frame");
        check(renderer.render(frame({0,0},0)).pixels == north.pixels, "Fresh device reproduces original pixels");
        renderer.resize(256);
        check(renderer.render(frame({0,0},0)).pixels == north.pixels, "Same-size resize preserves rendering");
        renderer.suspend();
        renderer.resize(128);
        check(empty(renderer.render(old_packet)), "Device recreation rejects old viewport geometry");
        context.viewport_size = 128;
        check(rgb(renderer.render(frame({0,0},0)),64,64,white), "Recreated device accepts new viewport frame");
        std::cout << "PASS: " << checks << " offscreen WARP pixel/transition checks; synthetic only, no game device/window/access.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Renderer host failed: " << error.what() << '\n';
        return 1;
    }
}
