# Experimental DXGI startup bridge

Original implementation. **Not installed or verified in SnowRunner.** This directory provides a concrete startup/COM ownership experiment, a renderer that writes to an owned swap-chain backbuffer, and an optional game bootstrap diagnostic. Passing these tests is not evidence of live telemetry, Steam overlay compatibility, or a working in-game minimap.

## Build and test

Windows, MSVC x64 and the Windows SDK are required. No packages or third-party code are downloaded.

```powershell
cmake -S experimental/dxgi -B build-dxgi -A x64
cmake --build build-dxgi --config Debug
ctest --test-dir build-dxgi -C Debug --output-on-failure
cmake --build build-dxgi --config Release
ctest --test-dir build-dxgi -C Release --output-on-failure
```

The normal build only wraps objects in the owned `srmm_dxgi_host.exe`. Its windows remain hidden. Other executables receive the system factory unchanged. The alignment host calls the renderer directly on its own swap chain.

The bridge DLL is named `dxgi.dll` alongside the owned executable, so normal Windows import resolution exercises the startup path. Its import library has a distinct name to avoid colliding with the SDK's `dxgi.lib`. The export table forwards 17 non-factory exports directly to the absolute System32 DXGI path configured on the build machine. Three factory exports call the system implementation and wrap supported returned interfaces. Forwarded names and ordinals are checked against the installed system library by the owned host. No renamed copy of Windows DXGI is required or distributed.

The absolute forwarding path makes these binaries specific to a compatible Windows installation with the same system directory. Build on the test machine. This is not a portable release package.

## Ownership and rendering

There is no custom `DllMain`, worker thread, remote thread, detour, executable-memory write, import rewrite or foreign-vtable patch. Initialization occurs when the application calls a factory export. The module retains its own reference until process exit; hot unloading is unsupported.

Factory and swap-chain wrappers are original COM objects with separate reference counts. They retain their underlying interfaces and the swap chain retains its factory wrapper. Known interface versions return the same IUnknown identity. Normal calls are delegated to the underlying interfaces, including Present and resize. This does not cache Steam's internal hook storage or claim ownership of it.

Rendering records commands on a plugin-owned deferred context, then calls `ExecuteCommandList(..., TRUE)` to preserve the immediate context's state. `FinishCommandList(FALSE)` returns the deferred context to defaults. Command lists, render-target views and backbuffer references are local to one draw; no backbuffer is cached across resize. The caller must serialize access to the application immediate context. This has been verified only in the owned host; a plugin-local lock cannot establish the game's threading contract.

The renderer accepts a north-up normalized map center and independent marker heading. A texture-storage vertical flip occurs after map panning and before texture sampling. It does not reverse heading. Original quadrant images test the flip, panning, north/east marker pixels, invalid frame rejection and backbuffer release. The bridge host additionally checks actual map/marker pixels, untouched surrounding pixels, topology/viewport/target restoration and reference cleanup.

Sources: [Windows DLL search order](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order), [factory creation](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-createdxgifactory1), [COM identity](https://learn.microsoft.com/en-us/windows/win32/api/unknwn/nf-unknwn-iunknown-queryinterface(refiid_void)), [command execution and state preservation](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-executecommandlist), [deferred context reset](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-finishcommandlist), [resize references](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-resizebuffers).

## Optional bootstrap diagnostic

```powershell
cmake -S experimental/dxgi -B build-dxgi -A x64 -DSRMM_BUILD_GAME_PROBE=ON
cmake --build build-dxgi --config Release
ctest --test-dir build-dxgi -C Release --output-on-failure
```

This creates a separate `build-dxgi/game-probe/Release/dxgi.dll`. That exact build is also exercised in an owned host. In addition to the owned executable, it can wrap factories in `SnowRunner.exe` **only if the executable SHA-256 matches the existing inspected-build fingerprint**. A matching hash is an eligibility check, not a compatibility guarantee.

The probe does not draw anything in SnowRunner and does not read game memory. It writes bounded counters (factory creation, swap creation, Present, resize, unsupported interface requests and release) to `%LOCALAPPDATA%/SnowRunnerMinimap/bootstrap.log`, at most 1 MiB. No raw addresses, pose or saves are logged and no network requests occur.

**No deployment occurs during build or test.** A future controlled startup test would add only this original DLL to the game's executable directory when no `dxgi.dll` already exists. It requires the user's go-ahead for a normal game exit/restart; preserve a fresh save backup first. The current session must not be killed or restarted automatically. Check startup, factory/swap/Present counters, normal exit and Steam overlay coexistence before enabling game rendering. If the game cannot start or interface compatibility fails, stop the attempt; with the game closed, move only the hash-verified added DLL out of its directory. Never overwrite another mod or remove an unrecognized DLL.

The old remote launcher and its byte compatibility guard are unchanged. This experiment does not relax that guard or retry the refused injection operation.

## Known integration limits

- Wrapping requires underlying `IDXGIFactory7` and `IDXGISwapChain4`; otherwise the returned object remains unwrapped. No game support is implied by successful compilation.
- Unknown/private interfaces are rejected with `E_NOINTERFACE`; callers that depend on them may be incompatible. This must be observed in the real startup test.
- Adapter/device methods can return an underlying factory outside the wrapper path. Present instrumentation will show whether the game's swap chain actually reaches this route; no hook is added to force it.
- The deferred renderer requires a device that supports deferred contexts. It rejects multisampled buffers and invalid/oversized viewports. Production use must handle device changes and the game's actual render thread.
- Present1 dirty-rectangle propagation is not implemented for game rendering. The game probe only delegates Present1, without drawing.
- World units, map origin, heading semantics, active gameplay/menu detection, movement and transitions remain unverified. No native candidate is promoted to a trusted frame.

## Local map extraction

`tools/extract_map_texture.py` reads one bounded, observed PCT map variant from an authorized local PAK and writes a new DDS outside the installation. It checks fields, dimensions, block format and every mip boundary. RGB decoding is supported; alpha and world calibration are not asserted. Keep output under ignored `assets/local/` or another private directory. No game texture belongs in this repository.

```powershell
python tools/extract_map_texture.py '<local shared_textures.pak>' level_us_01_02 assets/local/map.local.dds
```

The DDS preserves storage orientation. Use the renderer's explicit texture flip when alignment evidence calls for it. Texture orientation by itself does not establish the world-to-map affine transform.
