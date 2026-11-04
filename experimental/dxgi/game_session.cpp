#define NOMINMAX
#include "game_session.h"
#include "bootstrap_probe.h"
#include "diagnostic_view.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <locale>
#include <map>
#include <cmath>
#include <thread>
#include <mutex>

namespace {
bool hex(const std::string& s,std::array<std::uint8_t,32>& out) {
    if(s.size()!=64)return false;
    auto nib=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
    for(unsigned i=0;i<32;++i){int a=nib(s[i*2]),b=nib(s[i*2+1]);if(a<0||b<0)return false;out[i]=static_cast<std::uint8_t>(a*16+b);}return true;
}
std::wstring wide(const std::string& s) {
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if(!n)return {};std::wstring r(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),r.data(),n);return r;
}
template<class T> bool number(const std::string& s,T& value) {std::istringstream in(s);in.imbue(std::locale::classic());in>>value;return !in.fail()&&in.peek()==EOF;}
bool read_config(SrmmSessionConfig& config) {
    wchar_t root[32768]{};auto n=GetEnvironmentVariableW(L"LOCALAPPDATA",root,32768);if(!n||n>=32768)return false;
    const auto path=std::filesystem::path(root)/L"SnowRunnerMinimap"/L"session.local.ini";
    std::ifstream file(path,std::ios::binary);if(!file)return false;
    std::string bytes(16385,'\0');file.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));bytes.resize(static_cast<std::size_t>(file.gcount()));
    return srmm_parse_session(bytes,config);
}
std::array<std::uint8_t,32> build_hash() {std::array<std::uint8_t,32> b{};hex("d46623b23808d19a40f7ebeec15332d11f97317637ec4d4e072a501bc78a77c8",b);return b;}
}
bool srmm_parse_session(const std::string& bytes,SrmmSessionConfig& out) noexcept {
 try {
    if(bytes.empty()||bytes.size()>16384||bytes.find('\0')!=std::string::npos)return false;
    std::istringstream lines(bytes);std::string line;std::map<std::string,std::string> fields;
    while(std::getline(lines,line)) {if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;auto p=line.find('=');if(p==std::string::npos||!fields.emplace(line.substr(0,p),line.substr(p+1)).second)return false;}
    for(const auto& f:fields)if(f.first!="version"&&f.first!="enabled"&&f.first!="map_path"&&f.first!="packet_path"&&f.first!="map_sha256"&&f.first!="session"&&f.first!="map"&&f.first!="affine"&&f.first!="uv_per_pixel"&&f.first!="texture_v_flipped"&&f.first!="geometry"&&f.first!="viewport"&&f.first!="zoom"&&f.first!="tracking"&&f.first!="shape")return false;
    if(fields.at("version")!="1"||fields.at("enabled")!="1")return false;
    SrmmSessionConfig c;
    c.files.map_path=wide(fields.at("map_path"));c.files.packet_path=wide(fields.at("packet_path"));
    if(c.files.map_path.empty()||c.files.packet_path.empty()||!hex(fields.at("map_sha256"),c.files.map_sha256))return false;
    if(fields.at("session").empty()||fields.at("session").find_first_not_of("0123456789")!=std::string::npos||!number(fields.at("session"),c.session)||!c.session)return false;
    c.map=fields.at("map");if(c.map.size()<9||c.map.size()>54||c.map.compare(0,6,"level_"))return false;
    for(char v:c.map)if(!(v>='a'&&v<='z')&&!(v>='0'&&v<='9')&&v!='_')return false;
    std::istringstream affine(fields.at("affine"));affine.imbue(std::locale::classic());
    for(auto& v:c.calibration.affine)if(!(affine>>v)||!std::isfinite(v))return false;
    affine>>std::ws;if(!affine.eof())return false;
    auto& a=c.calibration.affine;if(std::abs(a[0]*a[4]-a[1]*a[3])<1e-14)return false;
    if(!number(fields.at("uv_per_pixel"),c.calibration.uv_per_pixel)||!std::isfinite(c.calibration.uv_per_pixel)||c.calibration.uv_per_pixel<=0||c.calibration.uv_per_pixel>1)return false;
    if(fields.at("texture_v_flipped")!="0"&&fields.at("texture_v_flipped")!="1")return false;
    c.calibration.texture_v_flipped=fields.at("texture_v_flipped")=="1";
    // Alignment preview is explicitly distinct from a validated calibration.
    if(fields.at("geometry")!="measured"&&fields.at("geometry")!="alignment_preview")return false;
    c.calibration.validated=fields.at("geometry")=="measured";
    std::istringstream viewport(fields.at("viewport"));viewport.imbue(std::locale::classic());
    if(!(viewport>>c.left>>c.top>>c.size))return false;viewport>>std::ws;if(!viewport.eof())return false;
    if(!std::isfinite(c.left)||!std::isfinite(c.top)||!std::isfinite(c.size)||c.left<0||c.top<0||c.size<96||c.size>800)return false;
    if(fields.count("zoom")&&(!number(fields.at("zoom"),c.zoom)||!std::isfinite(c.zoom)||c.zoom<.25f||c.zoom>8))return false;
    if(fields.count("tracking")){const auto& mode=fields.at("tracking");if(mode!="north"&&mode!="heading"&&mode!="motion")return false;c.tracking=mode=="motion"?2:mode=="heading"?1:0;}
    if(fields.count("shape")){if(fields.at("shape")!="circle"&&fields.at("shape")!="square")return false;c.square=fields.at("shape")=="square";}
    out=std::move(c);return true;
 }catch(...){return false;}
}
struct SrmmGameLayer::State {
    HANDLE stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::atomic<bool> admitted{true};
    struct Frame { SrmmSessionConfig config;std::shared_ptr<const SrmmIngestSnapshot> snapshot; };
    std::shared_ptr<const Frame> frame;
    ~State(){if(stop)CloseHandle(stop);}
};
struct SrmmGameLayer::View {
    const std::thread::id owner=std::this_thread::get_id();
    std::unique_ptr<SrmmDiagnosticView> diagnostic;
    int last_status=-1;
    bool visible=true,previous=false;
};
SrmmGameLayer::SrmmGameLayer():state_(std::make_shared<State>()) {
    if(!state_->stop)return;
    HMODULE module{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&worker),&module))return;
    auto* context=new std::shared_ptr<State>(state_);auto thread=CreateThread(nullptr,0,&worker,context,0,nullptr);
    if(thread)CloseHandle(thread);else delete context;
}
SrmmGameLayer::~SrmmGameLayer(){state_->admitted.store(false,std::memory_order_release);if(state_->stop)SetEvent(state_->stop);}
unsigned long __stdcall SrmmGameLayer::worker(void* opaque) noexcept {
    std::unique_ptr<std::shared_ptr<State>> holder(static_cast<std::shared_ptr<State>*>(opaque));auto state=*holder;holder.reset();
    try {
        SrmmSessionConfig config;if(!read_config(config)){srmm_probe_log("session-config-unavailable");return 0;}
        config.files.poll_ms=20;SrmmFileIngestor ingest(config.files);if(!ingest.start()){srmm_probe_log("session-start-refused");return 0;}
        srmm_probe_log("session-started",config.calibration.validated?1:0);
        std::uint64_t next_settings=0;
        while(WaitForSingleObject(state->stop,0)!=WAIT_OBJECT_0) {
            SrmmSessionConfig updated;
            // Reload display preferences only. Identity/assets/calibration stay fixed for the session.
            if(GetTickCount64()>=next_settings&&read_config(updated)&&updated.session==config.session&&updated.map==config.map){next_settings=GetTickCount64()+500;config.left=updated.left;config.top=updated.top;config.size=updated.size;config.zoom=updated.zoom;config.tracking=updated.tracking;config.square=updated.square;}
            auto frame=std::make_shared<State::Frame>();frame->config=config;frame->snapshot=ingest.snapshot();
            std::atomic_store_explicit(&state->frame,std::shared_ptr<const State::Frame>(frame),std::memory_order_release);
            if(WaitForSingleObject(state->stop,20)==WAIT_OBJECT_0)break;
        }
        ingest.request_stop();state->admitted.store(false,std::memory_order_release);
        srmm_probe_log(ingest.wait_for_stop(1000)?"session-stopped":"session-stop-pending");
    }catch(...){state->admitted.store(false,std::memory_order_release);srmm_probe_log("session-worker-error");}
    return 0;
}
void SrmmGameLayer::draw(IDXGISwapChain* swap,unsigned flags) noexcept {
    if((flags&DXGI_PRESENT_TEST)||!state_->admitted.load(std::memory_order_acquire))return;
    try {
        auto current=std::atomic_load_explicit(&state_->frame,std::memory_order_acquire);
        if(!current||!current->snapshot)return;
        if(!view_)view_=std::make_unique<View>();if(view_->owner!=std::this_thread::get_id())return;
        if(!view_->diagnostic) {
            const auto& c=current->config;
            view_->diagnostic=std::make_unique<SrmmDiagnosticView>(current->snapshot->generation,c.files.map_sha256,
                GetCurrentProcessId(),c.session,build_hash(),c.map,c.calibration,!c.calibration.validated,c.left,c.top,c.size);
        }
        // Explicit enabled configuration starts the diagnostic display when a fresh packet arrives.
        // No keyboard hooks or consumption; menus/garages are not inferred from pose availability.
        auto& visible=view_->visible;auto& previous=view_->previous;
        const bool key=(GetAsyncKeyState(VK_F9)&0x8000)!=0;
        if(key&&!previous)visible=!visible;previous=key;
        // Do not latch off on M/Escape: those keys are not a reliable menu-state signal.
        const auto& settings=current->config;view_->diagnostic->options(settings.left,settings.top,settings.size,settings.zoom,settings.tracking,settings.square);
        DXGI_SWAP_CHAIN_DESC desc{};const bool foreground=SUCCEEDED(swap->GetDesc(&desc))&&desc.OutputWindow==GetForegroundWindow();
        const auto result=view_->diagnostic->draw(swap,current->snapshot,GetTickCount64(),visible&&foreground);
        const int status=static_cast<int>(result.status)*32+static_cast<int>(result.packet_status);
        if(status!=view_->last_status){srmm_probe_log("session-view",status,static_cast<unsigned long>(result.graphics));view_->last_status=status;}
    }catch(...){srmm_probe_log("session-view-error");}
}
