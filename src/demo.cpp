#include "telemetry.hpp"
#include <cmath>
#include <iomanip>
#include <iostream>
using namespace minimap;
struct DemoSource : TelemetrySource {
    std::optional<TelemetrySample> sample;
    std::optional<TelemetrySample> poll() override {
        auto result=sample;
        sample.reset();
        return result;
    }
};
int main() {
    constexpr double pi=3.14159265358979323846;
    Calibration c{1,0,0,-1,250,250};
    BuildIdentity fixture{"synthetic","demo-v1",std::string(64,'a'),"synthetic","SVG"};
    MapContext context{{"demo-session","procedural-map","synthetic-truck"},c,220,1,true};
    FrameController controller(fixture);
    DemoSource source;
    std::cout<<std::fixed<<std::setprecision(2);
    std::cout<<"<svg xmlns='http://www.w3.org/2000/svg' width='1020' height='400' viewBox='0 0 1020 400'>"
             <<"<rect width='1020' height='400' fill='#121d20'/>"
             <<"<g font-family='sans-serif' fill='#edf3ed'><text x='24' y='34' font-size='22'>SnowRunner Minimap / geometry prototype</text>"
             <<"<text x='24' y='58' font-size='14'>SYNTHETIC DATA / no game connection / heading-up replay samples</text></g>";
    for(int frame=0;frame<4;++frame) {
        double h=frame*pi/2;
        source.sample=TelemetrySample{SourceKind::synthetic,ReadState::active,fixture,context.key,
                                      static_cast<std::uint64_t>(frame+1),frame*100.0,Pose{{0,0},h}};
        auto packet=controller.update(source,context,frame*100.0);
        if (!packet.geometry) return 1;
        View view=packet.geometry->view;
        std::cout<<"<defs><clipPath id='clip"<<frame<<"'><circle cx='110' cy='110' r='110'/></clipPath></defs>"
                 <<"<g transform='translate("<<24+frame*250<<",90)'>"
                 <<"<circle cx='110' cy='110' r='111' fill='#354c40' stroke='#b6cfbc'/>"
                 <<"<g clip-path='url(#clip"<<frame<<")'>";
        for(int y=-200;y<=200;y+=25) {
            std::cout<<"<polyline fill='none' stroke='#60765a' points='";
            for(int x=-200;x<=200;x+=5) {
                auto p=view.project(c.project({double(x),y+12*std::sin(x/35.0)}));
                std::cout<<p.x<<','<<p.y<<' ';
            }
            std::cout<<"'/>";
        }
        std::cout<<"<polyline fill='none' stroke='#dfbc78' stroke-width='5' points='";
        for(auto world : {Point{-140,-50},Point{-50,-50},Point{0,0},Point{30,85},Point{120,110}}) {
            auto p=view.project(c.project(world)); std::cout<<p.x<<','<<p.y<<' ';
        }
        std::cout<<"'/></g><path d='M110 97 L118 119 L110 115 L102 119 Z' fill='#f2f8ec'/>"
                 <<"<text x='110' y='253' text-anchor='middle' fill='#edf3ed' font-family='sans-serif'>Heading "<<frame*90<<" degrees</text></g>";
    }
    std::cout<<"<text x='24' y='382' font-family='sans-serif' fill='#b4c3bb'>Original procedural contours. No SnowRunner maps, textures, telemetry or executable code.</text></svg>\n";
}
