#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "game_session.h"
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
unsigned checks=0;
void check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
void put32(std::uint8_t* p,std::uint32_t value){for(unsigned i=0;i<4;++i)p[i]=static_cast<std::uint8_t>(value>>(i*8));}
void put64(std::uint8_t* p,std::uint64_t value){for(unsigned i=0;i<8;++i)p[i]=static_cast<std::uint8_t>(value>>(i*8));}
std::array<std::uint8_t,144> texture(bool blue){
    std::array<std::uint8_t,144> bytes{};std::memcpy(bytes.data(),"DDS ",4);
    put32(bytes.data()+4,124);put32(bytes.data()+8,0x81007);put32(bytes.data()+12,4);put32(bytes.data()+16,4);
    put32(bytes.data()+20,16);put32(bytes.data()+28,1);put32(bytes.data()+76,32);put32(bytes.data()+80,4);
    put32(bytes.data()+84,0x35545844);put32(bytes.data()+108,0x1000);
    if(blue)bytes[136]=bytes[138]=0x1f;else bytes[137]=bytes[139]=0xf8;
    return bytes;
}
std::string digest(const std::array<std::uint8_t,144>& bytes){
    std::array<std::uint8_t,32> hash{};check(srmm_sha256(bytes.data(),bytes.size(),hash),"Hash original fixture DDS");
    std::ostringstream text;text<<std::hex<<std::setfill('0');for(auto value:hash)text<<std::setw(2)<<unsigned(value);return text.str();
}
std::string utf8(const std::filesystem::path& path){
    const auto value=path.wstring();const auto size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    check(size>0,"Convert fixture path to UTF-8");std::string result(size,0);
    check(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),size,nullptr,nullptr)==size,"Encode fixture path exactly");return result;
}
struct Files {
    std::filesystem::path root,settings_dir,config,packet,map1,map2,late,next;
    Files(){
        wchar_t temporary[32768]{};check(GetTempPathW(32768,temporary)>0,"Resolve owned temporary root");
        root=std::filesystem::path(temporary)/(L"srmm-controller-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        check(CreateDirectoryW(root.c_str(),nullptr)!=FALSE,"Create exclusive owned root");
        settings_dir=root/L"SnowRunnerMinimap";check(CreateDirectoryW(settings_dir.c_str(),nullptr)!=FALSE,"Create owned settings directory");
        config=settings_dir/L"session.local.ini";packet=root/L"candidate.bin";map1=root/L"one.dds";map2=root/L"two.dds";late=root/L"late.dds";next=root/L"next.tmp";
        // This changes only the owned test process environment. Do not restore it
        // while an asynchronous controller shutdown could still read its path.
        check(SetEnvironmentVariableW(L"LOCALAPPDATA",root.c_str())!=FALSE,"Isolate controller config to owned fixture");
    }
    void replace(const std::filesystem::path& path,const void* bytes,std::size_t size){
        HANDLE file=CreateFileW(next.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(file!=INVALID_HANDLE_VALUE,"Create exclusive fixture update");DWORD written{};
        const bool saved=WriteFile(file,bytes,static_cast<DWORD>(size),&written,nullptr)&&written==size;CloseHandle(file);check(saved,"Write complete fixture update");
        bool published=false;const auto deadline=GetTickCount64()+500;
        do{
            if(MoveFileExW(next.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){published=true;break;}
            const auto error=GetLastError();if(error!=ERROR_SHARING_VIOLATION&&error!=ERROR_ACCESS_DENIED)break;
            Sleep(10);
        }while(GetTickCount64()<deadline);
        check(published,"Atomically publish owned fixture");
    }
    void text(const std::string& value){replace(config,value.data(),value.size());}
    void sample(const char* level,std::uint64_t sequence){
        std::array<std::uint8_t,256> bytes{};std::memcpy(bytes.data(),"SRMMTEL1",8);put32(bytes.data()+8,1);put32(bytes.data()+12,256);
        put32(bytes.data()+16,1);put32(bytes.data()+20,GetCurrentProcessId());put64(bytes.data()+24,sequence);put64(bytes.data()+32,GetTickCount64());put64(bytes.data()+40,99);
        std::memcpy(bytes.data()+48,level,std::strlen(level));const double values[]={1,0,2,0,0,1};std::memcpy(bytes.data()+144,values,sizeof(values));replace(packet,bytes.data(),bytes.size());
    }
    std::string session(const char* level,const std::filesystem::path& map,const std::string& hash,bool enabled=true,float zoom=1,std::uint64_t identity=99){
        std::ostringstream out;out.imbue(std::locale::classic());
        out<<"version=1\nenabled="<<(enabled?1:0)<<"\nmap_path="<<utf8(map)<<"\npacket_path="<<utf8(packet)<<"\nmap_sha256="<<hash
           <<"\nsession="<<identity<<"\nmap="<<level<<"\naffine=.001 0 .5 0 -.001 .5\nuv_per_pixel=.001\ntexture_v_flipped=1\ngeometry=alignment_preview\nviewport=32 32 320\nzoom="<<zoom<<"\n";return out.str();
    }
    ~Files(){
        // Exact test-owned paths only; never recursively delete a computed tree.
        DeleteFileW(config.c_str());DeleteFileW((settings_dir/L"session.local.lock").c_str());
        DeleteFileW(packet.c_str());DeleteFileW(map1.c_str());DeleteFileW(map2.c_str());DeleteFileW(late.c_str());DeleteFileW(next.c_str());
        RemoveDirectoryW(settings_dir.c_str());RemoveDirectoryW(root.c_str());
    }
};
template<class Predicate> SrmmSessionHealth await(SrmmGameLayer& layer,Predicate predicate,const char* message,DWORD duration=6000){
    const auto end=GetTickCount64()+duration;
    do{auto health=layer.health();if(predicate(health)){check(true,message);return health;}Sleep(10);}while(GetTickCount64()<end);
    throw std::runtime_error(message);
}
bool ready(const SrmmSessionHealth& health){return health.configured&&health.enabled&&health.admitted&&health.generation&&health.status==SrmmIngestStatus::ready;}
}
int main(){try{
    Files files;const auto red=texture(false),blue=texture(true);const auto red_hash=digest(red),blue_hash=digest(blue);
    files.replace(files.map1,red.data(),red.size());files.replace(files.map2,blue.data(),blue.size());files.sample("level_test_01",1);
    files.text(files.session("level_test_01",files.map1,red_hash,false));
    {
        SrmmGameLayer layer;
        await(layer,[](const auto& h){return h.configured&&!h.enabled&&!h.generation&&!h.admitted;},"Disabled startup retains config without admitting assets");
        files.text(files.session("level_test_01",files.map1,red_hash));
        const auto first=await(layer,[](const auto& h){return ready(h)&&h.map=="level_test_01";},"Enable starts actual background ingestion");
        files.text(files.session("level_test_01",files.map1,red_hash,true,2));
        const auto display=await(layer,[](const auto& h){return ready(h)&&h.zoom==2;},"Display preference reload reaches controller");
        check(display.generation==first.generation,"Display-only change preserves existing asset generation");

        files.sample("level_test_02",2);files.text(files.session("level_test_02",files.map2,blue_hash,true,2));
        const auto second=await(layer,[&](const auto& h){return ready(h)&&h.map=="level_test_02"&&h.generation!=first.generation;},"Map and asset switch creates a new ready generation");
        files.text(files.session("level_test_02",files.map2,blue_hash,false,2));
        await(layer,[](const auto& h){return h.configured&&!h.enabled&&!h.generation&&!h.admitted;},"Disable revokes active ingestion");
        files.text(files.session("level_test_02",files.map2,blue_hash,true,2));
        auto current=await(layer,[&](const auto& h){return ready(h)&&h.generation!=second.generation;},"Re-enable creates a new admitted generation");

        const auto good=files.session("level_test_02",files.map2,blue_hash,true,2);
        files.text(good+"enabled=1\n");
        await(layer,[](const auto& h){return h.configured&&!h.generation&&!h.admitted;},"Malformed configuration revokes ingestion");
        files.text(good);current=await(layer,[&](const auto& h){return ready(h)&&h.generation!=current.generation;},"Correcting malformed configuration recovers without controller restart");
        files.text(files.session("level_test_02",files.map2,blue_hash,true,2,100));
        await(layer,[](const auto& h){return !h.generation&&!h.admitted;},"Foreign session identity cannot replace the controller binding");
        files.text(good);current=await(layer,[&](const auto& h){return ready(h)&&h.generation!=current.generation;},"Original session recovers after rejecting replacement identity");

        files.sample("level_test_03",3);files.text(files.session("level_test_03",L"relative.dds",blue_hash));
        await(layer,[](const auto& h){return h.configured&&h.enabled&&h.map=="level_test_03"&&!h.generation&&!h.admitted;},"Refused nonlocal/relative asset does not terminate controller");
        files.text(files.session("level_test_03",files.map2,blue_hash));
        current=await(layer,[](const auto& h){return ready(h)&&h.map=="level_test_03";},"Corrected start refusal recovers after bounded backoff");

        files.sample("level_test_04",4);files.text(files.session("level_test_04",files.late,blue_hash));
        await(layer,[](const auto& h){return h.configured&&h.map=="level_test_04"&&!h.generation&&!h.admitted;},"Missing asset becomes unavailable without stale generation");
        files.replace(files.late,blue.data(),blue.size());
        current=await(layer,[](const auto& h){return ready(h)&&h.map=="level_test_04";},"Asset appearing later recovers without any config change");
        files.text(files.session("level_test_04",files.late,red_hash));
        await(layer,[](const auto& h){return h.configured&&h.map=="level_test_04"&&!h.generation&&!h.admitted;},"Hash mismatch revokes source generation");
        files.text(files.session("level_test_04",files.late,blue_hash));
        await(layer,[&](const auto& h){return ready(h)&&h.generation!=current.generation;},"Corrected asset fingerprint recovers without restart");
        files.text(files.session("level_test_04",files.late,blue_hash,false));
        await(layer,[](const auto& h){return h.configured&&!h.enabled&&!h.generation&&!h.admitted;},"Final disable leaves no admitted reader before destruction");
    }
    std::cout<<"PASS "<<checks<<" actual controller reload, generation, disable and recovery checks; owned headless fixtures only.\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

