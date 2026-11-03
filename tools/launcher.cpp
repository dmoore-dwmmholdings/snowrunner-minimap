#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>
#include "diagnostic_api.h"
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
const char* expected_game_hash = "d46623b23808d19a40f7ebeec15332d11f97317637ec4d4e072a501bc78a77c8";
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE input = nullptr) : value(input) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
[[noreturn]] void fail(const char* stage) {
    throw std::runtime_error(std::string(stage) + " (Win32 " + std::to_string(GetLastError()) + ")");
}
std::filesystem::path own_directory() {
    std::vector<wchar_t> buffer(32768);
    DWORD count = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!count || count >= buffer.size()) fail("Host path");
    return std::filesystem::path(std::wstring(buffer.data(), count)).parent_path();
}
std::filesystem::path process_path(HANDLE process) {
    std::vector<wchar_t> buffer(32768);
    DWORD count = static_cast<DWORD>(buffer.size());
    if (!QueryFullProcessImageNameW(process, 0, buffer.data(), &count)) fail("Target path");
    return std::filesystem::path(std::wstring(buffer.data(), count));
}
std::string sha256(const std::filesystem::path& path) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    auto status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status < 0) throw std::runtime_error("SHA256 provider unavailable.");
    try {
        status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
        if (status < 0) throw std::runtime_error("SHA256 initialization failed.");
        std::ifstream stream(path, std::ios::binary);
        if (!stream) throw std::runtime_error("Cannot open artifact for hashing.");
        std::array<char, 65536> bytes{};
        while (stream) {
            stream.read(bytes.data(), bytes.size());
            if (stream.gcount() && BCryptHashData(hash, reinterpret_cast<PUCHAR>(bytes.data()),
                                                static_cast<ULONG>(stream.gcount()), 0) < 0)
                throw std::runtime_error("SHA256 update failed.");
        }
        if (!stream.eof()) throw std::runtime_error("Artifact read failed.");
        std::array<unsigned char, 32> digest{};
        if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
            throw std::runtime_error("SHA256 finalization failed.");
        std::ostringstream text;
        for (auto byte : digest) text << std::hex << std::setfill('0') << std::setw(2) << unsigned(byte);
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return text.str();
    } catch (...) {
        if (hash) BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw;
    }
}
struct Module { std::filesystem::path path; uintptr_t base; DWORD size; };
std::vector<Module> modules(DWORD pid) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
    if (snapshot.value == INVALID_HANDLE_VALUE) fail("Module snapshot");
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Module32FirstW(snapshot.value, &entry)) fail("Module enumeration");
    std::vector<Module> result;
    do {
        result.push_back({entry.szExePath, reinterpret_cast<uintptr_t>(entry.modBaseAddr), entry.modBaseSize});
    } while (Module32NextW(snapshot.value, &entry));
    return result;
}
bool same_path(const std::filesystem::path& a, const std::filesystem::path& b) {
    return _wcsicmp(a.lexically_normal().c_str(), b.lexically_normal().c_str()) == 0;
}
uintptr_t find_module(DWORD pid, const std::filesystem::path& path) {
    for (const auto& module : modules(pid)) if (same_path(module.path, path)) return module.base;
    return 0;
}
void describe_entry_branch(DWORD pid, HANDLE process, uintptr_t address,
                           const unsigned char* bytes, const char* label) {
    uintptr_t destination = 0;
    const char* kind = "unrecognized entry (not followed)";
    if (bytes[0] == 0xe9) {
        int32_t displacement = 0;
        std::memcpy(&displacement, bytes + 1, sizeof(displacement));
        destination = address + 5 + displacement;
        kind = "relative jump";
    } else if (bytes[0] == 0xff && bytes[1] == 0x25) {
        int32_t displacement = 0;
        std::memcpy(&displacement, bytes + 2, sizeof(displacement));
        SIZE_T received = 0;
        if (!ReadProcessMemory(process, reinterpret_cast<void*>(address + 6 + displacement),
                               &destination, sizeof(destination), &received) || received != sizeof(destination))
            destination = 0;
        kind = "indirect jump";
    }
    std::cout << label << ": " << kind;
    if (destination) {
        for (const auto& module : modules(pid)) {
            if (destination >= module.base && destination - module.base < module.size) {
                std::wcout << L" into " << module.path.filename().wstring() << L'\n';
                return;
            }
        }
        std::cout << " into memory outside enumerated modules";
    }
    std::cout << '\n'; // Attribution only; never follow, patch or execute the destination.
}
uintptr_t remote_system_function(DWORD pid, HANDLE process, const char* name) {
    FARPROC function = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), name);
    if (!function) fail("System export");
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(function), &owner)) fail("System export owner");
    const uintptr_t local_address = reinterpret_cast<uintptr_t>(function);
    for (const auto& local : modules(GetCurrentProcessId())) {
        if (local.base != reinterpret_cast<uintptr_t>(owner)) continue;
        for (const auto& remote : modules(pid)) {
            if (!same_path(local.path, remote.path) || local.size != remote.size) continue;
            uintptr_t offset = local_address - local.base;
            if (offset + 32 > remote.size) throw std::runtime_error("System export outside matching module.");
            std::array<unsigned char, 32> bytes{};
            SIZE_T received = 0;
            if (!ReadProcessMemory(process, reinterpret_cast<void*>(remote.base + offset), bytes.data(), bytes.size(), &received) ||
                received != bytes.size()) fail("System export read failed; refusing remote call");
            if (std::memcmp(bytes.data(), reinterpret_cast<void*>(local_address), bytes.size())) {
                const auto* local_bytes = reinterpret_cast<const unsigned char*>(local_address);
                size_t first = 0;
                while (first < bytes.size() && bytes[first] == local_bytes[first]) ++first;
                std::wcout << L"Mismatching system module: " << remote.path.wstring() << L'\n';
                std::cout << "Export " << name << " RVA 0x" << std::hex << offset
                          << " first differing byte +0x" << first << std::dec << '\n';
                describe_entry_branch(GetCurrentProcessId(), GetCurrentProcess(), local_address, local_bytes, "Local entry");
                describe_entry_branch(pid, process, remote.base + offset, bytes.data(), "Target entry");
                throw std::runtime_error("System export bytes differ; refusing remote call.");
            }
            return remote.base + offset;
        }
    }
    throw std::runtime_error("No matching system module for remote export; no ASLR address assumption.");
}
void check_x64(HANDLE process) {
    USHORT process_machine = 0, native_machine = 0;
    if (!IsWow64Process2(process, &process_machine, &native_machine)) fail("Architecture query");
    if (process_machine != IMAGE_FILE_MACHINE_UNKNOWN || native_machine != IMAGE_FILE_MACHINE_AMD64)
        throw std::runtime_error("Only native x64 Windows targets are supported.");
}
void remote_call(HANDLE process, uintptr_t function, void* argument, bool& finished) {
    finished = false;
    Handle thread(CreateRemoteThread(process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(function), argument, 0, nullptr));
    if (!thread.value) { finished = true; fail("Remote thread creation"); }
    DWORD wait = WaitForSingleObject(thread.value, 15000);
    if (wait != WAIT_OBJECT_0)
        throw std::runtime_error("Remote operation outcome UNCERTAIN; do not retry or unload. Target was not terminated.");
    finished = true;
}
uintptr_t load_diagnostic(HANDLE process, DWORD pid, const std::filesystem::path& dll) {
    if (find_module(pid, dll)) throw std::runtime_error("Diagnostic already loaded; ownership unknown, refusing.");
    uintptr_t loader = remote_system_function(pid, process, "LoadLibraryW");
    std::wstring path = dll.wstring();
    const SIZE_T size = (path.size() + 1) * sizeof(wchar_t);
    void* memory = VirtualAllocEx(process, nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!memory) fail("Remote path allocation");
    bool finished = true;
    try {
        SIZE_T written = 0;
        if (!WriteProcessMemory(process, memory, path.c_str(), size, &written) || written != size) fail("Remote path write");
        remote_call(process, loader, memory, finished);
        VirtualFreeEx(process, memory, 0, MEM_RELEASE);
        memory = nullptr;
        uintptr_t module = 0;
        try { module = find_module(pid, dll); }
        catch (...) { throw std::runtime_error("Remote load completed but module enumeration failed; outcome UNCERTAIN. Do not retry."); }
        if (!module) throw std::runtime_error("LoadLibrary completed but diagnostic module was not found.");
        return module; // Do not truncate a 64-bit HMODULE to a thread exit DWORD.
    } catch (...) {
        if (memory && finished) VirtualFreeEx(process, memory, 0, MEM_RELEASE);
        throw; // If the thread is still running, retain its path buffer until target exits.
    }
}
void unload_diagnostic(HANDLE process, DWORD pid, const std::filesystem::path& dll, uintptr_t module) {
    if (find_module(pid, dll) != module) throw std::runtime_error("Module identity changed before unload.");
    bool finished = false;
    remote_call(process, remote_system_function(pid, process, "FreeLibrary"), reinterpret_cast<void*>(module), finished);
    if (find_module(pid, dll)) throw std::runtime_error("Module remains loaded; no repeated decrement attempted.");
}
void self_test(const std::filesystem::path& directory, const std::filesystem::path& dll) {
    std::wstring prefix = L"Local\\SRMM_fixture_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64());
    Handle ready(CreateEventW(nullptr, TRUE, FALSE, (prefix + L"_ready").c_str()));
    Handle done(CreateEventW(nullptr, TRUE, FALSE, (prefix + L"_done").c_str()));
    Handle stop(CreateEventW(nullptr, TRUE, FALSE, (prefix + L"_stop").c_str()));
    if (!ready.value || !done.value || !stop.value) fail("Fixture events");
    auto target = directory / L"srmm_fixture_target.exe";
    std::wstring command = L"\"" + target.wstring() + L"\" \"" + prefix + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(target.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, directory.c_str(), &startup, &child)) fail("Fixture creation");
    Handle process(child.hProcess), thread(child.hThread);
    try {
        if (WaitForSingleObject(ready.value, 5000) != WAIT_OBJECT_0) throw std::runtime_error("Fixture did not become ready.");
        check_x64(process.value);
        uintptr_t module = load_diagnostic(process.value, child.dwProcessId, dll);
        if (WaitForSingleObject(done.value, 5000) != WAIT_OBJECT_0) throw std::runtime_error("Fixture API exercise failed.");
        unload_diagnostic(process.value, child.dwProcessId, dll, module);
        SetEvent(stop.value);
        if (WaitForSingleObject(process.value, 5000) != WAIT_OBJECT_0) throw std::runtime_error("Fixture did not exit.");
        DWORD code = 99;
        if (!GetExitCodeProcess(process.value, &code) || code != 0) throw std::runtime_error("Fixture reported failure.");
        std::cout << "PASS: own hidden fixture created; diagnostic loaded; synthetic ABI exercised; DLL unloaded; child exited.\n";
    } catch (...) {
        SetEvent(stop.value);
        if (WaitForSingleObject(process.value, 1000) != WAIT_OBJECT_0) {
            // Only this child was created by us. Never terminate the game.
            TerminateProcess(process.value, 9);
            WaitForSingleObject(process.value, 1000);
        }
        throw;
    }
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        auto directory = own_directory();
        auto dll = directory / L"srmm_diagnostic.dll";
        if (argc == 2 && std::wstring(argv[1]) == L"--self-test") {
            std::cout << "SELF-TEST ONLY / no SnowRunner access\nDLL SHA256 " << sha256(dll) << '\n';
            self_test(directory, dll);
            return 0;
        }
        if (argc != 3 || (std::wstring(argv[1]) != L"--inspect" && std::wstring(argv[1]) != L"--diagnostic-load")) {
            std::cout << "Usage: srmm_launcher --self-test | --inspect PID | --diagnostic-load PID\n"
                         "Diagnostic-load runs an inert DLL load/unload test, NOT a live minimap.\n";
            return argc == 1 ? 0 : 2;
        }
        size_t consumed = 0;
        unsigned long parsed = std::stoul(argv[2], &consumed);
        if (!parsed || consumed != std::wstring(argv[2]).size()) throw std::runtime_error("Invalid PID.");
        DWORD pid = static_cast<DWORD>(parsed);
        const bool load = std::wstring(argv[1]) == L"--diagnostic-load";
        DWORD rights = PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE;
        if (load) rights |= PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE;
        Handle process(OpenProcess(rights, FALSE, pid));
        if (!process.value) fail("OpenProcess; no privilege elevation or bypass attempted");
        auto exe = process_path(process.value);
        if (_wcsicmp(exe.filename().c_str(), L"SnowRunner.exe"))
            throw std::runtime_error("Target executable name is not SnowRunner.exe.");
        check_x64(process.value);
        std::string hash = sha256(exe);
        if (hash != expected_game_hash)
            throw std::runtime_error("Target is not the exact inspected SnowRunner build.");
        std::wcout << L"Verified target: " << exe.wstring() << L" PID " << pid << L'\n';
        std::cout << "Game SHA256 " << hash << "\nPresent graphics modules (not active API proof):\n";
        for (const auto& module : modules(pid)) {
            auto name = module.path.filename().wstring();
            if (!_wcsicmp(name.c_str(), L"d3d11.dll") || !_wcsicmp(name.c_str(), L"d3d12.dll") || !_wcsicmp(name.c_str(), L"dxgi.dll"))
                std::wcout << L"  " << module.path.wstring() << L'\n';
        }
        if (!load) {
            std::cout << "Diagnostic module currently " << (find_module(pid, dll) ? "PRESENT" : "absent") << '\n';
            for (const char* name : {"LoadLibraryW", "FreeLibrary"}) {
                try {
                    remote_system_function(pid, process.value, name);
                    std::cout << name << ": read-only compatibility check passed\n";
                } catch (const std::exception& error) {
                    std::cout << name << ": " << error.what() << '\n';
                }
            }
            return 0; // Inspection succeeds even when compatibility is reported as unavailable.
        }
        std::cout << "INERT DIAGNOSTIC ONLY / no hooks, workers, game API calls or live minimap\nDLL SHA256 " << sha256(dll) << '\n';
        uintptr_t module = load_diagnostic(process.value, pid, dll);
        unload_diagnostic(process.value, pid, dll, module);
        std::cout << "Verified diagnostic load and unload. No game files changed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Launcher stopped: " << error.what() << '\n';
        return 1;
    }
}
