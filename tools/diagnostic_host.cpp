#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "diagnostic_api.h"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
bool nearly_equal(double a, double b) { return std::abs(a - b) < 1e-9; }
std::filesystem::path plugin_path() {
    std::vector<wchar_t> buffer(32768);
    const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!size || size >= buffer.size()) throw std::runtime_error("Cannot resolve host executable path.");
    return std::filesystem::path(std::wstring(buffer.data(), size)).parent_path() / L"srmm_diagnostic.dll";
}
struct Library {
    HMODULE handle;
    explicit Library(const std::filesystem::path& path)
        : handle(LoadLibraryExW(path.c_str(), nullptr,
                              LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        if (!handle) throw std::runtime_error("DLL load failed, Win32 error " + std::to_string(GetLastError()));
    }
    ~Library() { if (handle) FreeLibrary(handle); }
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
    void close() {
        if (!FreeLibrary(handle)) throw std::runtime_error("DLL unload failed.");
        handle = nullptr;
    }
};
struct Instance {
    const SrmmApi& api;
    SrmmSession handle = nullptr;
    explicit Instance(const SrmmApi& value) : api(value) {
        check(api.create(&handle) == SRMM_OK && handle, "Session creation failed.");
    }
    ~Instance() { api.destroy(handle); }
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;
};
SrmmSample sample(uint64_t sequence = 1, double now = 100) {
    return {sizeof(SrmmSample), SRMM_SYNTHETIC, 1, SRMM_ACTIVE,
            sequence, now, now, 10, 20, 0};
}
SrmmFrame evaluate(Instance& instance, const SrmmSample& input, uint32_t state, bool visible) {
    SrmmFrame frame{};
    frame.size = sizeof(frame);
    check(instance.api.update(instance.handle, &input, &frame) == SRMM_OK, "Update failed.");
    check(frame.state == state && frame.has_geometry == (visible ? 1u : 0u), "Unexpected frame state.");
    if (!visible) check(frame.image_center_x == 0 && frame.image_center_y == 0 && frame.player_x == 0 &&
                        frame.player_y == 0 && frame.view_heading == 0 && frame.marker_heading == 0,
                        "Hidden frame retained geometry.");
    return frame;
}
void exercise(const SrmmApi& api) {
    check(api.capabilities == SRMM_CAP_SYNTHETIC_ONLY, "Unexpected plugin capabilities.");
    check(api.create(nullptr) == SRMM_BAD_ARGUMENT, "Null session output accepted.");
    api.destroy(nullptr);
    {
        Instance first(api), independent(api);
        auto input = sample();
        auto frame = evaluate(first, input, SRMM_READY, true);
        check(nearly_equal(frame.image_center_x, 260) && nearly_equal(frame.image_center_y, 230), "Fixture calibration mismatch.");
        check(nearly_equal(frame.player_x, 110) && nearly_equal(frame.player_y, 110) && nearly_equal(frame.marker_heading, 0),
              "Fixture player projection mismatch.");
        input.has_event = 0;
        evaluate(independent, input, SRMM_NO_POSE, false);
        input.now_ms = 600;
        evaluate(first, input, SRMM_READY, true);
        input.now_ms = 1101;
        evaluate(first, input, SRMM_STALE, false);
    }
    {
        Instance instance(api);
        auto input = sample();
        input.heading_radians = 3.14159265358979323846 / 2;
        auto frame = evaluate(instance, input, SRMM_READY, true);
        check(nearly_equal(frame.view_heading, input.heading_radians) && nearly_equal(frame.marker_heading, 0),
              "Synthetic heading-up rotation mismatch.");
        input = sample(2, 200);
        input.source = SRMM_NATIVE_CANDIDATE;
        evaluate(instance, input, SRMM_UNSUPPORTED_SOURCE, false);
        input.has_event = 0;
        evaluate(instance, input, SRMM_UNSUPPORTED_SOURCE, false);
    }
    for (uint32_t state = SRMM_MENU; state <= SRMM_READ_ERROR; ++state) {
        Instance instance(api);
        evaluate(instance, sample(), SRMM_READY, true);
        auto input = sample(2, 200);
        input.state = state;
        evaluate(instance, input, SRMM_FRAME_MENU + state - SRMM_MENU, false);
    }
    {
        Instance instance(api);
        evaluate(instance, sample(), SRMM_READY, true);
        evaluate(instance, sample(), SRMM_OUT_OF_ORDER, false);
        auto input = sample(2, 200);
        input.east = std::numeric_limits<double>::quiet_NaN();
        evaluate(instance, input, SRMM_INVALID_POSE, false);
        input = sample(3, 300);
        input.sampled_ms = 301;
        evaluate(instance, input, SRMM_INVALID_POSE, false);
        input = sample(4, 250);
        evaluate(instance, input, SRMM_CLOCK_ERROR, false);
    }
    {
        Instance instance(api);
        evaluate(instance, sample(), SRMM_READY, true);
        auto input = sample(2, 200);
        input.size = sizeof(input) - 1;
        SrmmFrame frame{};
        frame.size = sizeof(frame);
        frame.has_geometry = 1;
        check(api.update(instance.handle, &input, &frame) == SRMM_BAD_ARGUMENT && !frame.has_geometry,
              "Malformed sample did not hide output.");
        input = sample(2, 200);
        check(api.update(instance.handle, &input, &frame) == SRMM_SESSION_POISONED && !frame.has_geometry,
              "Malformed session resumed without recreation.");
    }
    {
        Instance instance(api);
        auto input = sample();
        SrmmFrame wrong{};
        wrong.size = sizeof(wrong) - 1;
        wrong.has_geometry = 17;
        check(api.update(instance.handle, &input, &wrong) == SRMM_BAD_ARGUMENT && wrong.has_geometry == 17,
              "Wrong-sized output was modified.");
    }
    {
        Instance instance(api);
        auto input = sample();
        input.source = 999;
        SrmmFrame frame{};
        frame.size = sizeof(frame);
        check(api.update(instance.handle, &input, &frame) == SRMM_BAD_ARGUMENT && !frame.has_geometry,
              "Unknown source accepted.");
    }
}
} // namespace

int main(int argc, char**) {
    if (argc != 1) {
        std::cerr << "No arguments accepted: host loads only its adjacent srmm_diagnostic.dll.\n";
        return 2;
    }
    try {
        const auto path = plugin_path();
        std::wcout << L"SYNTHETIC ONLY / no game connection\nLoading into this test host: " << path.wstring() << L'\n';
        for (int cycle = 0; cycle < 3; ++cycle) {
            Library library(path);
            auto get_api = reinterpret_cast<SrmmGetApi>(GetProcAddress(library.handle, "srmm_get_api"));
            check(get_api != nullptr, "Missing diagnostic export.");
            SrmmApi api{};
            check(get_api(SRMM_DIAGNOSTIC_ABI + 1, sizeof(api), &api) == SRMM_ABI_MISMATCH && !api.create,
                  "Unsupported ABI version accepted.");
            check(get_api(SRMM_DIAGNOSTIC_ABI, sizeof(api) - 1, &api) == SRMM_BAD_ARGUMENT,
                  "Wrong API size accepted.");
            check(get_api(SRMM_DIAGNOSTIC_ABI, sizeof(api), nullptr) == SRMM_BAD_ARGUMENT,
                  "Null API accepted.");
            check(get_api(SRMM_DIAGNOSTIC_ABI, sizeof(api), &api) == SRMM_OK &&
                  api.size == sizeof(api) && api.version == SRMM_DIAGNOSTIC_ABI && api.create && api.update && api.destroy,
                  "API negotiation failed.");
            exercise(api); // All sessions destroyed before close.
            api = {};
            get_api = nullptr;
            library.close();
            check(GetModuleHandleW(path.c_str()) == nullptr, "DLL remained loaded after release.");
        }
        std::cout << checks << " diagnostic checks passed across 3 load/unload cycles.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Diagnostic failed: " << error.what() << '\n';
        return 1;
    }
}
