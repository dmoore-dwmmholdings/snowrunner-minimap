#define NOMINMAX
#include "game_session.h"
#include "bootstrap_probe.h"
#include "diagnostic_view.h"
#include "native_settings.h"
#include "display_persistence.h"
#include "menu_state_packet.h"
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
std::filesystem::path session_path() {
    wchar_t root[32768]{};auto n=GetEnvironmentVariableW(L"LOCALAPPDATA",root,32768);if(!n||n>=32768)return {};
    return std::filesystem::path(root)/L"SnowRunnerMinimap"/L"session.local.ini";
}
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
    for(const auto& f:fields)if(f.first!="version"&&f.first!="enabled"&&f.first!="map_path"&&f.first!="packet_path"&&f.first!="destination_path"&&f.first!="ui_state_path"&&f.first!="map_sha256"&&f.first!="session"&&f.first!="map"&&f.first!="affine"&&f.first!="uv_per_pixel"&&f.first!="uv_per_pixel_v"&&f.first!="texture_v_flipped"&&f.first!="geometry"&&f.first!="viewport"&&f.first!="zoom"&&f.first!="tracking"&&f.first!="shape")return false;
    if(fields.at("version")!="1"||(fields.at("enabled")!="1"&&fields.at("enabled")!="0"))return false;
    SrmmSessionConfig c;c.enabled=fields.at("enabled")=="1";
    c.files.map_path=wide(fields.at("map_path"));c.files.packet_path=wide(fields.at("packet_path"));
    if(fields.count("ui_state_path"))c.files.ui_state_path=wide(fields.at("ui_state_path"));
    if(fields.count("destination_path"))c.files.destination_path=wide(fields.at("destination_path"));
    if(c.files.map_path.empty()||c.files.packet_path.empty()||!hex(fields.at("map_sha256"),c.files.map_sha256))return false;
    if(fields.at("session").empty()||fields.at("session").find_first_not_of("0123456789")!=std::string::npos||!number(fields.at("session"),c.session)||!c.session)return false;
    c.map=fields.at("map");if(c.map.size()<9||c.map.size()>54||c.map.compare(0,6,"level_"))return false;
    for(char v:c.map)if(!(v>='a'&&v<='z')&&!(v>='0'&&v<='9')&&v!='_')return false;
    std::istringstream affine(fields.at("affine"));affine.imbue(std::locale::classic());
    for(auto& v:c.calibration.affine)if(!(affine>>v)||!std::isfinite(v))return false;
    affine>>std::ws;if(!affine.eof())return false;
    auto& a=c.calibration.affine;if(std::abs(a[0]*a[4]-a[1]*a[3])<1e-14)return false;
    if(!number(fields.at("uv_per_pixel"),c.calibration.uv_per_pixel)||!std::isfinite(c.calibration.uv_per_pixel)||c.calibration.uv_per_pixel<=0||c.calibration.uv_per_pixel>1)return false;
    if(fields.count("uv_per_pixel_v")&&(!number(fields.at("uv_per_pixel_v"),c.calibration.uv_per_pixel_v)||!std::isfinite(c.calibration.uv_per_pixel_v)||c.calibration.uv_per_pixel_v<=0||c.calibration.uv_per_pixel_v>1))return false;
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
bool srmm_same_map_binding(const SrmmSessionConfig& a,const SrmmSessionConfig& b) noexcept {
    return a.session==b.session&&a.map==b.map&&a.files.map_path==b.files.map_path&&
        a.files.packet_path==b.files.packet_path&&a.files.destination_path==b.files.destination_path&&a.files.ui_state_path==b.files.ui_state_path&&a.files.map_sha256==b.files.map_sha256&&
        a.calibration.affine==b.calibration.affine&&a.calibration.uv_per_pixel==b.calibration.uv_per_pixel&&a.calibration.uv_per_pixel_v==b.calibration.uv_per_pixel_v&&
        a.calibration.texture_v_flipped==b.calibration.texture_v_flipped&&a.calibration.validated==b.calibration.validated;
}
struct SrmmGameLayer::State {
    HANDLE stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::atomic<bool> admitted{true};
    struct Frame { SrmmSessionConfig config;std::shared_ptr<const SrmmIngestSnapshot> snapshot;std::shared_ptr<const SrmmDisplaySettings> applied_preferences,attempted_preferences;SrmmPreferencesResult preference_result=SrmmPreferencesResult::saved; };
    std::shared_ptr<const Frame> frame;
    std::shared_ptr<const SrmmDisplaySettings> requested_preferences;
    ~State(){if(stop)CloseHandle(stop);}
};
struct SrmmGameLayer::View {
    const std::thread::id owner=std::this_thread::get_id();
    std::unique_ptr<SrmmDiagnosticView> diagnostic;
    int last_status=-1;
    std::uint64_t generation{};
    bool visible=true,previous=false;
    SrmmNativeSettings panel;
    std::unique_ptr<SrmmUiStateGate> ui_gate;
    std::uint64_t ui_generation{};
    std::shared_ptr<const SrmmDisplaySettings> pending_preferences;
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
        SrmmSessionConfig config;while(!read_config(config)){if(WaitForSingleObject(state->stop,500)==WAIT_OBJECT_0)return 0;}
        const auto session=config.session;const auto packet_path=config.files.packet_path;
        std::unique_ptr<SrmmFileIngestor> ingest;
        std::uint64_t next_settings=0,next_save=0,next_ingest=0;
        bool pending_stop=false;
        std::shared_ptr<const SrmmDisplaySettings> applied,attempted;
        SrmmPreferencesResult preference_result=SrmmPreferencesResult::saved;
        auto stop_ingest=[&](){
            std::atomic_store_explicit(&state->frame,std::shared_ptr<const State::Frame>{},std::memory_order_release);
            if(!ingest)return true;
            ingest->request_stop();
            // Never accumulate workers when an OS read cannot finish cancellation.
            const bool stopped=ingest->wait_for_stop(1000);if(!stopped)pending_stop=true;ingest.reset();return stopped;
        };
        while(WaitForSingleObject(state->stop,0)!=WAIT_OBJECT_0) {
            const auto now=GetTickCount64();
            const auto requested=std::atomic_load_explicit(&state->requested_preferences,std::memory_order_acquire);
            if(requested&&requested!=applied&&now>=next_save){
                next_save=now+200;
                const auto result=srmm_persist_display(session_path().wstring(),session,packet_path,*requested);
                attempted=requested;preference_result=result;
                if(result==SrmmPreferencesResult::saved){applied=requested;next_settings=0;srmm_probe_log("display-preferences-saved");}
            }
            if(now>=next_settings) {
                next_settings=now+500;SrmmSessionConfig updated;
                const bool valid=read_config(updated)&&updated.session==session&&updated.files.packet_path==packet_path;
                if(!valid) {if(!stop_ingest())break;}
                else {
                    if(ingest&&!srmm_same_map_binding(config,updated)&&!stop_ingest())break;
                    config=std::move(updated);config.files.poll_ms=20;
                    if(!config.enabled){if(!stop_ingest())break;}
                    else if(!ingest&&now>=next_ingest){
                        ingest=std::make_unique<SrmmFileIngestor>(config.files);
                        if(!ingest->start()){ingest.reset();next_ingest=now+2000;srmm_probe_log("session-start-refused");}
                        else srmm_probe_log("session-map-generation",static_cast<unsigned long>(ingest->generation()));
                    }
                }
            }
            if(ingest&&ingest->snapshot()->status==SrmmIngestStatus::asset_error){
                if(!stop_ingest())break;next_ingest=now+2000;
            }
            {
                auto frame=std::make_shared<State::Frame>();frame->config=config;frame->snapshot=ingest?ingest->snapshot():nullptr;frame->applied_preferences=applied;frame->attempted_preferences=attempted;frame->preference_result=preference_result;
                std::atomic_store_explicit(&state->frame,std::shared_ptr<const State::Frame>(frame),std::memory_order_release);
            }
            if(WaitForSingleObject(state->stop,20)==WAIT_OBJECT_0)break;
        }
        const bool stopped=stop_ingest();state->admitted.store(false,std::memory_order_release);
        srmm_probe_log(stopped&&!pending_stop?"session-stopped":"session-stop-pending");
    }catch(...){state->admitted.store(false,std::memory_order_release);srmm_probe_log("session-worker-error");}
    return 0;
}
SrmmSessionHealth SrmmGameLayer::health() const noexcept {
    SrmmSessionHealth result;
    try {
        const auto frame=std::atomic_load_explicit(&state_->frame,std::memory_order_acquire);
        if(!frame)return result;
        result.configured=true;result.enabled=frame->config.enabled;result.map=frame->config.map;result.zoom=frame->config.zoom;
        if(frame->snapshot){
            result.generation=frame->snapshot->generation;result.status=frame->snapshot->status;
            result.admitted=state_->admitted.load(std::memory_order_acquire)&&frame->snapshot->admission&&frame->snapshot->admission->load(std::memory_order_acquire);
        }
    }catch(...){return {};}
    return result;
}
void SrmmGameLayer::draw(IDXGISwapChain* swap,unsigned flags) noexcept {
    if((flags&DXGI_PRESENT_TEST)||!state_->admitted.load(std::memory_order_acquire))return;
    try {
        auto current=std::atomic_load_explicit(&state_->frame,std::memory_order_acquire);
        if(!current)return;
        if(!view_)view_=std::make_unique<View>();if(view_->owner!=std::this_thread::get_id())return;
        DXGI_SWAP_CHAIN_DESC desc{};if(FAILED(swap->GetDesc(&desc)))return;
        const bool foreground=desc.OutputWindow==GetForegroundWindow();
        const auto& c=current->config;
        if(view_->pending_preferences==current->applied_preferences)view_->pending_preferences.reset();
        SrmmDisplaySettings settings{c.left,c.top,c.size,c.zoom,c.tracking,c.square};
        if(view_->pending_preferences)settings=*view_->pending_preferences;
        unsigned width=desc.BufferDesc.Width,height=desc.BufferDesc.Height;
        if(!width||!height){RECT rect{};if(GetClientRect(desc.OutputWindow,&rect)){width=rect.right-rect.left;height=rect.bottom-rect.top;}}
        bool hud_allowed=c.files.ui_state_path.empty(),menu_visible=false;
        if(!c.files.ui_state_path.empty()&&current->snapshot){
            if(!view_->ui_gate||view_->ui_generation!=current->snapshot->generation){
                view_->ui_gate=std::make_unique<SrmmUiStateGate>(GetCurrentProcessId(),c.session,build_hash());
                view_->ui_generation=current->snapshot->generation;
            }
            const auto ui=current->snapshot->have_ui_state?view_->ui_gate->consume(current->snapshot->ui_state.data(),128,GetTickCount64()):SrmmUiStateResult{};
            hud_allowed=ui.status==SrmmUiPacketStatus::ready&&ui.state==SrmmUiState::gameplay;
            menu_visible=ui.status==SrmmUiPacketStatus::ready&&ui.state==SrmmUiState::menu;
        }
        if(auto changed=view_->panel.update(srmm_poll_settings_input(desc.OutputWindow,menu_visible),settings,width,height)){
            settings=*changed;view_->pending_preferences=std::make_shared<const SrmmDisplaySettings>(settings);
            std::atomic_store_explicit(&state_->requested_preferences,view_->pending_preferences,std::memory_order_release);
        }
        const bool key=foreground&&(GetAsyncKeyState(VK_F9)&0x8000)!=0;
        if(key&&!view_->previous)view_->visible=!view_->visible;view_->previous=key;
        std::optional<std::uint64_t> saved_age;
        if(current->snapshot){
            if(!view_->diagnostic||view_->generation!=current->snapshot->generation){
                view_->diagnostic.reset();view_->generation=current->snapshot->generation;view_->last_status=-1;
                view_->diagnostic=std::make_unique<SrmmDiagnosticView>(current->snapshot->generation,c.files.map_sha256,
                    GetCurrentProcessId(),c.session,build_hash(),c.map,c.calibration,!c.calibration.validated,c.left,c.top,c.size);
            }
            view_->diagnostic->options(settings.left,settings.top,settings.size,settings.zoom,settings.tracking,settings.square);
            // Optional verified UI sidecar; no key/pose/cursor inference. Unknown configured state hides only the minimap.
            const auto result=view_->diagnostic->draw(swap,current->snapshot,GetTickCount64(),c.enabled&&view_->visible&&foreground&&hud_allowed);
            const int status=static_cast<int>(result.status)*32+static_cast<int>(result.packet_status);
            if(status!=view_->last_status){srmm_probe_log("session-view",status,static_cast<unsigned long>(result.graphics));view_->last_status=status;}
            if(const auto modified=view_->diagnostic->saved_modified_ms()){
                FILETIME ft{};GetSystemTimeAsFileTime(&ft);ULARGE_INTEGER value{};value.LowPart=ft.dwLowDateTime;value.HighPart=ft.dwHighDateTime;
                const auto unix_ms=value.QuadPart/10000-11644473600000ULL;if(unix_ms>=*modified)saved_age=(unix_ms-*modified)/1000;
            }
        }else{view_->diagnostic.reset();view_->generation=0;}
        auto save_status=current->applied_preferences?SrmmSettingsSaveStatus::saved:SrmmSettingsSaveStatus::loaded;
        if(view_->pending_preferences){
            save_status=SrmmSettingsSaveStatus::pending;
            if(current->attempted_preferences==view_->pending_preferences&&current->preference_result!=SrmmPreferencesResult::saved&&current->preference_result!=SrmmPreferencesResult::busy)save_status=SrmmSettingsSaveStatus::failed;
        }
        view_->panel.preference_status(save_status);
        view_->panel.navigation_status(!c.calibration.validated,saved_age);
        if(foreground&&view_->panel.open()){
            const auto result=view_->panel.draw(swap);if(FAILED(result))srmm_probe_log("settings-panel-error",static_cast<unsigned long>(result));
        }
    }catch(...){srmm_probe_log("session-view-error");}
}
