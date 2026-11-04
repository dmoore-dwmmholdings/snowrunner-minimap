#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "display_persistence.h"
#include "game_session.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
unsigned checks=0;
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~Handle(){if(value!=INVALID_HANDLE_VALUE&&value)CloseHandle(value);}
    void close(){if(value!=INVALID_HANDLE_VALUE&&value)CloseHandle(value);value=INVALID_HANDLE_VALUE;}
};
struct Files {
    std::filesystem::path root,config,lock;
    Files(){
        wchar_t temp[32768]{};check(GetTempPathW(32768,temp)>0,"Resolve owned temporary directory");
        root=std::filesystem::path(temp)/(L"srmm-display-persistence-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        check(CreateDirectoryW(root.c_str(),nullptr)!=FALSE,"Create exclusive fixture directory");
        config=root/L"session.local.ini";lock=root/L"session.local.lock";
    }
    ~Files(){DeleteFileW(config.c_str());DeleteFileW(lock.c_str());RemoveDirectoryW(root.c_str());}
    void write(const std::string& bytes){std::ofstream output(config,std::ios::binary|std::ios::trunc);output.write(bytes.data(),bytes.size());check(bool(output),"Write own session fixture");}
    std::string read()const{std::ifstream input(config,std::ios::binary);check(bool(input),"Read own session fixture");return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};}
    void no_temporaries()const{for(const auto& entry:std::filesystem::directory_iterator(root))check(entry.path()==config||entry.path()==lock,"No temporary file survives persistence result");}
};
std::string fixture(bool enabled=true){return std::string("version=1\nenabled=")+(enabled?"1":"0")+
    "\nmap_path=C:\\fixture\\second.dds\npacket_path=C:\\fixture\\candidate.bin\ndestination_path=C:\\fixture\\destinations.bin\nmap_sha256="+std::string(64,'b')+
    "\nsession=99\nmap=level_test_02\naffine=.001 0 .25 0 -.002 .75\nuv_per_pixel=.001\nuv_per_pixel_v=.002\ntexture_v_flipped=1\ngeometry=alignment_preview\nviewport=32 48 320\nzoom=1\ntracking=motion\nshape=circle\n";}
SrmmSessionConfig parse(const std::string& bytes){SrmmSessionConfig result;check(srmm_parse_session(bytes,result),"Parse saved session fixture");return result;}
}
int main(){try{
    Files files;const std::wstring packet=L"C:\\fixture\\candidate.bin";
    const SrmmDisplaySettings preferences{80,96,384,2,1,true};
    files.write(fixture(false));const auto disabled_before=parse(files.read());
    check(srmm_persist_display(files.config.wstring(),99,packet,preferences)==SrmmPreferencesResult::saved,"Persist original display preferences");
    const auto disabled_after=parse(files.read());
    check(!disabled_after.enabled,"Native preferences cannot re-enable a disabled session");
    check(srmm_same_map_binding(disabled_before,disabled_after),"Display update preserves current map assets, calibration and source binding");
    check(disabled_after.left==80&&disabled_after.top==96&&disabled_after.size==384&&disabled_after.zoom==2&&disabled_after.tracking==1&&disabled_after.square,"All requested display values persisted");
    files.no_temporaries();

    const auto before=files.read();
    check(srmm_persist_display(files.config.wstring(),100,packet,preferences)==SrmmPreferencesResult::wrong_context,"Replacement session rejects stale settings write");
    check(files.read()==before,"Wrong session leaves exact file unchanged");
    check(srmm_persist_display(files.config.wstring(),99,L"C:\\fixture\\other.bin",preferences)==SrmmPreferencesResult::wrong_context,"Wrong packet source rejected");
    check(files.read()==before,"Wrong source leaves exact file unchanged");
    auto invalid=preferences;invalid.zoom=0;
    check(srmm_persist_display(files.config.wstring(),99,packet,invalid)==SrmmPreferencesResult::invalid,"Invalid display preference rejected before write");
    check(files.read()==before,"Invalid preference leaves file unchanged");

    Handle held;held.value=CreateFileW(files.lock.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(held.value!=INVALID_HANDLE_VALUE,"Open shared writer lock");OVERLAPPED region{};
    check(LockFileEx(held.value,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&region)!=FALSE,"Acquire own byte-zero writer lock");
    const auto start=GetTickCount64();
    check(srmm_persist_display(files.config.wstring(),99,packet,preferences)==SrmmPreferencesResult::busy,"Contended writer returns Busy");
    check(GetTickCount64()-start<1000,"Contended write does not block control thread");
    check(files.read()==before,"Contended write preserves exact config");held.close();
    check(srmm_persist_display(files.config.wstring(),99,packet,preferences)==SrmmPreferencesResult::saved,"Persistence recovers when writer releases lock");
    files.no_temporaries();

    // Emulate a map switch committed under the shared writer protocol before save.
    auto changed=fixture(false);changed.replace(changed.find("level_test_02"),13,"level_test_03");
    changed.replace(changed.find("second.dds"),10,"third.dds");files.write(changed);
    const auto changed_before=parse(files.read());
    check(srmm_persist_display(files.config.wstring(),99,packet,preferences)==SrmmPreferencesResult::saved,"Apply pending display edit to latest map configuration");
    const auto changed_after=parse(files.read());
    check(!changed_after.enabled&&changed_after.map=="level_test_03"&&srmm_same_map_binding(changed_before,changed_after),"Map-switch identity and disabled admission survive pending native edit");

    const auto good=files.read();Handle reader;
    reader.value=CreateFileW(files.config.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(reader.value!=INVALID_HANDLE_VALUE,"Hold an ordinary rename-conflicting reader");
    check(srmm_persist_display(files.config.wstring(),99,packet,preferences)==SrmmPreferencesResult::io_error,"Atomic replacement refusal is reported");
    check(files.read()==good,"Failed replacement preserves original configuration");files.no_temporaries();reader.close();
    check(srmm_persist_display(files.config.wstring(),99,packet,preferences)==SrmmPreferencesResult::saved,"Atomic replacement recovers after ordinary sharing conflict");

    files.write(good+"enabled=1\n");const auto malformed=files.read();
    check(srmm_persist_display(files.config.wstring(),99,packet,preferences)==SrmmPreferencesResult::invalid,"Malformed configuration is not normalized or overwritten");
    check(files.read()==malformed,"Malformed file remains exactly unchanged");files.no_temporaries();
    std::cout<<"PASS "<<checks<<" native display persistence, identity, disabled-state and lock checks; owned fixtures only.\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
