#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "bootstrap_probe.h"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cstdio>
#include <cwchar>
#include <mutex>

bool srmm_verified_game() noexcept {
#ifndef SRMM_ENABLE_GAME_PROBE
    return false;
#else
    static const bool valid=[]() noexcept {
        wchar_t path[32768]{};
        if(!GetModuleFileNameW(nullptr,path,32768))return false;
        const auto* slash=wcsrchr(path,L'\\');
        if(_wcsicmp(slash?slash+1:path,L"SnowRunner.exe")!=0)return false;
        HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return false;
        BCRYPT_ALG_HANDLE alg{};BCRYPT_HASH_HANDLE hash{};
        bool ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
        if(ok)ok=BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0;
        std::array<unsigned char,65536> bytes{};DWORD count{};
        while(ok) { if(!ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)){ok=false;break;}if(!count)break;ok=BCryptHashData(hash,bytes.data(),count,0)>=0; }
        std::array<unsigned char,32> digest{};
        if(ok)ok=BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0;
        if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);CloseHandle(file);
        constexpr std::array<unsigned char,32> expected={0xd4,0x66,0x23,0xb2,0x38,0x08,0xd1,0x9a,0x40,0xf7,0xeb,0xee,0xc1,0x53,0x32,0xd1,0x1f,0x97,0x31,0x76,0x37,0xec,0x4d,0x4e,0x07,0x2a,0x50,0x1b,0xc7,0x8a,0x77,0xc8};
        return ok&&digest==expected;
    }();
    return valid;
#endif
}
void srmm_probe_log(const char* event,unsigned long long a,unsigned long long b) noexcept {
#ifdef SRMM_ENABLE_GAME_PROBE
    if(!srmm_verified_game())return;
    // Private local counters only. No raw addresses, pose, saves or network I/O.
    static std::mutex lock;
    try {
        std::lock_guard<std::mutex> hold(lock);
        wchar_t path[32768]{};
        const auto length=GetEnvironmentVariableW(L"LOCALAPPDATA",path,32600);
        if(!length||length>=32600)return;
        wcscat_s(path,L"\\SnowRunnerMinimap");
        if(!CreateDirectoryW(path,nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return;
        wcscat_s(path,L"\\bootstrap.log");
        HANDLE file=CreateFileW(path,FILE_APPEND_DATA|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return;
        LARGE_INTEGER size{};
        if(GetFileSizeEx(file,&size)&&size.QuadPart<1048576) {
            char line[256]{};
            const auto n=sprintf_s(line,"%llu pid=%lu thread=%lu %s %llu %llu\r\n",GetTickCount64(),GetCurrentProcessId(),GetCurrentThreadId(),event,a,b);
            DWORD written{};if(n>0)WriteFile(file,line,static_cast<DWORD>(n),&written,nullptr);
        }
        CloseHandle(file);
    } catch(...) { /* Diagnostics must not change application flow. */ }
#else
    (void)event;(void)a;(void)b;
#endif
}
