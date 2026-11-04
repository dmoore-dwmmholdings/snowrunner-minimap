#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "display_persistence.h"
#include "game_session.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <locale>
#include <map>
namespace {
struct Handle {HANDLE h=INVALID_HANDLE_VALUE;~Handle(){if(h!=INVALID_HANDLE_VALUE&&h)CloseHandle(h);}};
std::string number(float value){std::ostringstream out;out.imbue(std::locale::classic());out.precision(9);out<<value;return out.str();}
}
SrmmPreferencesResult srmm_persist_display(const std::wstring& name,std::uint64_t session,
    const std::wstring& packet_path,const SrmmDisplaySettings& settings) noexcept {
 try {
    if(!srmm_valid_display_settings(settings))return SrmmPreferencesResult::invalid;
    const std::filesystem::path path(name);if(!path.is_absolute())return SrmmPreferencesResult::invalid;
    Handle lock;lock.h=CreateFileW((path.parent_path()/L"session.local.lock").c_str(),GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(lock.h==INVALID_HANDLE_VALUE)return SrmmPreferencesResult::io_error;
    OVERLAPPED region{};
    if(!LockFileEx(lock.h,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&region))
        return GetLastError()==ERROR_LOCK_VIOLATION?SrmmPreferencesResult::busy:SrmmPreferencesResult::io_error;
    std::ifstream input(path,std::ios::binary);if(!input)return SrmmPreferencesResult::io_error;
    std::string bytes(16385,'\0');input.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));bytes.resize(static_cast<std::size_t>(input.gcount()));input.close();
    SrmmSessionConfig config;if(!srmm_parse_session(bytes,config))return SrmmPreferencesResult::invalid;
    if(config.session!=session||config.files.packet_path!=packet_path)return SrmmPreferencesResult::wrong_context;
    std::map<std::string,std::string> changes{{"zoom",number(settings.zoom)},{"shape",settings.square?"square":"circle"},
        {"tracking",settings.tracking==2?"motion":settings.tracking==1?"heading":"north"},
        {"viewport",number(settings.left)+" "+number(settings.top)+" "+number(settings.size)}};
    std::istringstream lines(bytes);std::string line,result;
    while(std::getline(lines,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;
        const auto end=line.find('=');const auto key=line.substr(0,end);const auto found=changes.find(key);
        if(found!=changes.end()){result+=key+"="+found->second+"\n";changes.erase(found);}else result+=line+"\n";
    }
    for(const auto& entry:changes)result+=entry.first+"="+entry.second+"\n";
    if(result.size()>16384)return SrmmPreferencesResult::invalid;
    const auto temporary=path.parent_path()/(L"display-next-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".local.tmp");
    Handle file;file.h=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file.h==INVALID_HANDLE_VALUE)return SrmmPreferencesResult::io_error;
    DWORD written{};const bool saved=WriteFile(file.h,result.data(),static_cast<DWORD>(result.size()),&written,nullptr)&&written==result.size()&&FlushFileBuffers(file.h);
    CloseHandle(file.h);file.h=INVALID_HANDLE_VALUE;
    if(!saved){DeleteFileW(temporary.c_str());return SrmmPreferencesResult::io_error;}
    if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        DeleteFileW(temporary.c_str());return SrmmPreferencesResult::io_error;
    }
    return SrmmPreferencesResult::saved;
 }catch(...){return SrmmPreferencesResult::io_error;}
}
