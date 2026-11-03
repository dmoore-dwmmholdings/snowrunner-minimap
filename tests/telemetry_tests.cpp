#include "telemetry.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
using namespace minimap;
namespace {
int checks=0;
void check(bool ok, const std::string& name) {
    ++checks;
    if (!ok) throw std::runtime_error(name);
}
struct FakeSource : TelemetrySource {
    std::optional<TelemetrySample> event;
    bool throws=false;
    std::optional<TelemetrySample> poll() override {
        if (throws) throw std::runtime_error("fake read failure");
        auto result=event;
        event.reset();
        return result;
    }
};
BuildIdentity build() { return {"synthetic-store","fixture-v1",std::string(64,'a'),"fixture-x64","fixture-DX11"}; }
MapContext map() { return {{"session-1","synthetic-map-1","truck-1"},{2,0,0,-2,200,200},220,1,true}; }
TelemetrySample sample(std::uint64_t sequence=1, double time=100) {
    return {SourceKind::synthetic,ReadState::active,build(),map().key,sequence,time,Pose{{10,20},0}};
}
void hidden(RenderPacket packet, FrameState expected, const std::string& name) {
    check(packet.state==expected && !packet.geometry,name);
}
template<class F> void rejected_config(F fn) {
    bool threw=false;
    try { fn(); } catch(const std::invalid_argument&) { threw=true; }
    check(threw,"invalid configuration");
}
}
int main() {
    try {
        FakeSource source;
        FrameController controller(build());
        auto context=map();
        hidden(controller.update(source,context,100),FrameState::no_pose,"no initial data");
        source.event=sample();
        auto ready=controller.update(source,context,100);
        check(ready.state==FrameState::synthetic_ready && ready.geometry.has_value(),"ready synthetic frame");
        check(std::string(frame_status(ready.state)).find("SYNTHETIC")!=std::string::npos,"visible synthetic label");
        check(ready.geometry->player.x==110 && ready.geometry->player.y==110,"centered marker");
        check(ready.geometry->context==context.key,"render frame context");
        check(controller.update(source,context,1099).geometry.has_value(),"redraw latest sample until stale");
        hidden(controller.update(source,context,1100),FrameState::stale,"stale boundary clears marker");
        hidden(controller.update(source,context,1101),FrameState::stale,"stale remains hidden");
        source.event=sample(2,1200);
        check(controller.update(source,context,1200).geometry.has_value(),"new sample recovers");
        source.event=sample(2,1201);
        hidden(controller.update(source,context,1201),FrameState::out_of_order,"duplicate invalidates cache");
        source.event=sample(1,1202);
        hidden(controller.update(source,context,1202),FrameState::out_of_order,"older sequence rejected");
        for(auto state : {ReadState::menu,ReadState::garage,ReadState::loading,ReadState::disconnected,ReadState::read_error}) {
            FakeSource s;
            FrameController c(build());
            s.event=sample(); c.update(s,context,100);
            s.event=TelemetrySample{}; s.event->state=state;
            auto frame=c.update(s,context,101);
            check(frame.state!=FrameState::synthetic_ready && !frame.geometry,"transition clears marker");
            check(!c.update(s,context,102).geometry,"transition cannot replay old marker");
        }
        // Every identity component must match; no wildcard or version-only support.
        for(int field=0;field<5;++field) {
            FakeSource s; FrameController c(build()); s.event=sample();
            auto& b=s.event->build;
            if(field==0) b.storefront="other";
            if(field==1) b.version="other";
            if(field==2) b.executable_sha256=std::string(64,'b');
            if(field==3) b.architecture="other";
            if(field==4) b.graphics_api="other";
            hidden(c.update(s,context,100),FrameState::wrong_build,"exact build mismatch");
        }
        for(int field=0;field<3;++field) {
            FakeSource s; FrameController c(build()); s.event=sample();
            auto& key=s.event->context;
            if(field==0) key.session="old-session";
            if(field==1) key.map="old-map";
            if(field==2) key.vehicle="old-truck";
            hidden(c.update(s,context,100),FrameState::wrong_context,"context mismatch");
        }
        for(int field=0;field<5;++field) {
            FakeSource s; FrameController c(build()); s.event=sample();
            double nan=std::numeric_limits<double>::quiet_NaN();
            if(field==0) s.event->pose.reset();
            if(field==1) s.event->pose->world.x=nan;
            if(field==2) s.event->pose->heading=nan;
            if(field==3) s.event->sampled_ms=101;
            if(field==4) s.event->sampled_ms=nan;
            hidden(c.update(s,context,100),FrameState::invalid_pose,"invalid pose or timestamp");
        }
        FakeSource s; FrameController c(build());
        s.event=sample(); s.event->source=SourceKind::native_candidate;
        hidden(c.update(s,context,100),FrameState::unsupported_source,"native remains unavailable even with matching identity");
        s.event=sample(2,101); c.update(s,context,101);
        auto changed=context; changed.key.session="session-2";
        hidden(c.update(s,changed,102),FrameState::no_pose,"session change clears previous pose");
        s.event=sample();
        hidden(c.update(s,changed,103),FrameState::wrong_context,"late old-session event rejected");
        s.event=sample(); s.event->context=changed.key; s.event->sampled_ms=104;
        check(c.update(s,changed,104).geometry.has_value(),"new session may restart sequence");
        hidden(c.update(s,changed,103),FrameState::clock_error,"clock rollback clears marker");
        hidden(c.update(s,changed,105),FrameState::clock_error,"clock failure never revives cache");
        s.event=sample(2,106); s.event->context=changed.key;
        check(c.update(s,changed,106).geometry.has_value(),"new event after clock recovery");
        s.throws=true;
        hidden(c.update(s,changed,107),FrameState::read_error,"adapter exception hidden");
        s.throws=false;
        hidden(c.update(s,changed,108),FrameState::read_error,"exception clears cached marker");
        auto bad=context; bad.zoom=0;
        hidden(c.update(s,bad,109),FrameState::invalid_context,"bad viewport");
        bad=context; bad.calibration={1,2,2,4,0,0};
        hidden(c.update(s,bad,110),FrameState::invalid_context,"bad map calibration");
        rejected_config([]{FrameController invalid(build(),0);});
        rejected_config([]{auto b=build(); b.executable_sha256="not-a-hash"; FrameController invalid(b);});
        // Synthetic fixture verifies calibrated marker rotation in north-up mode.
        FakeSource north_source; FrameController north(build()); context.heading_up=false;
        north_source.event=sample(); north_source.event->pose->heading=1.5707963267948966;
        auto frame=north.update(north_source,context,100);
        check(frame.geometry && std::abs(frame.geometry->marker_heading-1.5707963267948966)<1e-8,"north-up marker heading");
        std::cout<<checks<<" adapter checks passed\n";
    } catch(const std::exception& e) {
        std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';
        return 1;
    }
}
