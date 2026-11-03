# Isolated Windows diagnostic plugin

Follow-up: the [live validation checkpoint](live-validation.md) adds an original guarded launcher and reports the actual game load attempt. This document records the earlier isolated DLL baseline.

An original diagnostic DLL and a console test host are implemented. They exercise the existing synthetic frame controller across a real Windows DLL boundary. They do not connect to SnowRunner and are not an ASI/proxy loader, telemetry reader or renderer.

## Build and run

From the repository on x64 Windows with the Visual Studio 2022 C++ workload:

```powershell
cmake -S . -B build -A x64 -DSRMM_BUILD_DIAGNOSTICS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\srmm_diagnostic_host.exe
```

Diagnostics default on for Windows; use `-DSRMM_BUILD_DIAGNOSTICS=OFF` to build only the portable prototype. The diagnostic ABI is restricted to x64 Windows. The existing portable targets remain independent of this Windows-only option.

Local Release artifacts on DWM-GAMING:

- `C:\Coding\random_ideas\snowrunner-minimap\build\Release\srmm_diagnostic.dll`
- `C:\Coding\random_ideas\snowrunner-minimap\build\Release\srmm_diagnostic_host.exe`

Both files must be from the trusted local build and stay together outside the game installation. The host accepts no command-line arguments and resolves its own executable directory, not the current working directory. It loads exactly the adjacent `srmm_diagnostic.dll` into **its own process**. It does not search for or open a game process. Local artifact SHA-256 hashes and build details are recorded in ignored `build/diagnostic-manifest.local.json`; generated binaries are not committed.

## Loading and ABI

The host calls `LoadLibraryExW` with an absolute DLL path and `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32`. This limits dependency lookup to the DLL directory and System32 under the documented loader rules; it is not authentication or a sandbox for an untrusted DLL. See Microsoft's [LoadLibraryExW reference](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryexw). The built Release DLL depends on Microsoft C++/UCRT runtime libraries and Kernel32; it has no graphics, game, networking or third-party mod-library dependency. The target machine must supply compatible Microsoft runtimes. Debug artifacts also require the debug runtime and are not a deployment package.

`include/diagnostic_api.h` defines ABI v1 with one exported entry point, `srmm_get_api`. Negotiation requires an exact version and table size. The table contains create/update/destroy functions and advertises only the synthetic capability. Fixed-width scalar fields, x64 layout checks and C linkage avoid exposing C++ containers or allocator ownership. A session is allocated and freed inside the same DLL. The API still requires valid pointers and live handles from a trusted caller; arbitrary addresses, use-after-destroy, overlapping buffers and hostile in-process callers are outside the contract.

The caller owns input/output structs. A request contains synthetic east/north position, clockwise heading from north, sequence and times in one monotonic clock domain. The fixture build identity and map/session/vehicle context are fixed within the diagnostic DLL. This intentionally does not expose configurable game addresses or claim real coordinate conventions. A no-event request polls the existing cache; its pose/source payload does not create a new event.

The DLL passes each event through the existing `FrameController`. Native candidates remain unsupported. Menu/garage/loading/disconnect/read-error events hide the marker, as do stale or invalid poses. Valid output is cleared before an update. Malformed ABI requests or caught exceptions poison the session, preventing reuse of a previously cached marker; destroy and recreate it to resume. A wrongly sized output buffer is not written and must not be rendered. No exceptions are intentionally allowed across the API.

There is no custom `DllMain`, automatic session, worker thread, hook, callback registration, file/network logging or game access. Initialization happens only through the negotiated API. CRT initialization still runs as part of normal DLL loading. This follows Microsoft's advice to minimize work under the loader lock in [DLL best practices](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices).

The API is single-threaded. The host destroys all sessions before calling `FreeLibrary`, discards its function pointers, checks that the DLL is no longer loaded, and repeats the full cycle three times. The API is not a cross-compiler, cross-architecture or future-version compatibility promise.

## Verified checkpoint

On 2026-11-03, MSVC 19.44 / Windows SDK 10.0.26100.0 x64 Debug and Release builds succeeded. Both configurations passed **4/4 CTest cases**: 27 geometry checks, 48 frame-contract checks, 11 PE inspection checks, and 258 diagnostic checks across three load/unload cycles. The 258 count includes repeated checks across those cycles, not 258 distinct scenarios.

The host verifies ABI rejection, fixture projection and heading, independent sessions, cached/stale frames, native-source rejection, transition hiding, sequence/future/invalid/rollback handling, malformed request poisoning and untouched wrong-sized output. The host was launched by CTest with a working directory different from its binary directory. Two additional manual negative checks confirmed that an isolated copy without its adjacent DLL exits 1 (Win32 error 126), and an arbitrary argument exits 2 before loading. The temporary copy was removed. Release PE inspection confirmed the sole export `srmm_get_api` and the runtime dependencies described above. The separate 23 Python metadata tests were unchanged and were not rerun for this C++ addition.

This does not test SnowRunner compatibility, live pose, graphics state, a game thread callback, publisher support, another Windows installation, concurrent API calls, forced allocation failures or crash recovery. No game attachment, DLL loading into SnowRunner, game-file edit, anti-cheat bypass or dependency download occurred.

## Deployment boundary and rollback

The prepared loading action is only `srmm_diagnostic_host.exe` loading its adjacent DLL. It is already tested and needs no game changes. Closing the host releases process-owned resources; normal completion explicitly destroys sessions and unloads the DLL. Removing the two generated artifacts after the host exits removes this diagnostic build. No service, registry entry, configuration file or persistent game state is created by either artifact.

There is no game deployment manifest because no game loader or game initialization entry point has been implemented. Renaming this DLL to `.asi` would not initialize the diagnostic API and is not a proposed installation method. Do not put either file into `SnowRunner\Sources\Bin`.

The next native-integration proposal must name the exact loader/build, game files to add, initialization/shutdown behavior, any process access or hooks, observed renderer assumptions and rollback. It must resolve the evidence gaps in [pose and loading feasibility](pose-and-loading.md) and obtain specific action-time authorization before touching the game. A working standalone DLL boundary does not resolve those gaps. No such runtime action was attempted or denied in this checkpoint.
