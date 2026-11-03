#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "diagnostic_api.h"
#include <string>

// Cooperative child for our launcher's test only. Never launches the game.
int wmain(int argc, wchar_t** argv) {
    if (argc != 2 || std::wstring(argv[1]).find(L"Local\\SRMM_fixture_") != 0) return 2;
    std::wstring prefix(argv[1]);
    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, (prefix + L"_ready").c_str());
    HANDLE done = OpenEventW(EVENT_MODIFY_STATE, FALSE, (prefix + L"_done").c_str());
    HANDLE stop = OpenEventW(SYNCHRONIZE, FALSE, (prefix + L"_stop").c_str());
    if (!ready || !done || !stop) return 3;
    SetEvent(ready);
    bool tested = false;
    bool passed = false;
    ULONGLONG deadline = GetTickCount64() + 30000;
    while (GetTickCount64() < deadline && WaitForSingleObject(stop, 20) == WAIT_TIMEOUT) {
        HMODULE module = GetModuleHandleW(L"srmm_diagnostic.dll");
        if (!module || tested) continue;
        tested = true;
        auto get_api = reinterpret_cast<SrmmGetApi>(GetProcAddress(module, "srmm_get_api"));
        SrmmApi api{};
        SrmmSession session = nullptr;
        if (get_api && get_api(SRMM_DIAGNOSTIC_ABI, sizeof(api), &api) == SRMM_OK &&
            api.create && api.update && api.destroy && api.capabilities == SRMM_CAP_SYNTHETIC_ONLY &&
            api.create(&session) == SRMM_OK) {
            SrmmSample sample{sizeof(SrmmSample), SRMM_SYNTHETIC, 1, SRMM_ACTIVE, 1, 100, 100, 10, 20, 0};
            SrmmFrame frame{};
            frame.size = sizeof(frame);
            passed = api.update(session, &sample, &frame) == SRMM_OK && frame.state == SRMM_READY &&
                     frame.has_geometry == 1 && frame.image_center_x == 260 && frame.image_center_y == 230;
            api.destroy(session);
        }
        api = {};
        get_api = nullptr;
        session = nullptr;
        // No plugin calls or owned sessions remain after this signal.
        SetEvent(done);
    }
    CloseHandle(ready);
    CloseHandle(done);
    CloseHandle(stop);
    return passed ? 0 : 8;
}
