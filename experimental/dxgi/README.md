# Experimental DXGI startup bridge

Original implementation. **Experimental; no working in-game minimap is established.** This directory provides a concrete startup/COM ownership experiment, a renderer that writes to an owned swap-chain backbuffer, and an optional game bootstrap diagnostic. Passing these tests is not evidence of live telemetry, Steam overlay compatibility, or a working in-game minimap.

## Build and test

Windows, MSVC x64 and the Windows SDK are required. No packages or third-party code are downloaded.

```powershell
cmake -S experimental/dxgi -B build-dxgi -G "Visual Studio 17 2022" -A x64
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
cmake -S experimental/dxgi -B build-dxgi -G "Visual Studio 17 2022" -A x64 -DSRMM_BUILD_GAME_PROBE=ON
cmake --build build-dxgi --config Release
ctest --test-dir build-dxgi -C Release --output-on-failure
```

This creates a separate `build-dxgi/game-probe/Release/dxgi.dll`. That exact build is also exercised in an owned host. In addition to the owned executable, it can wrap factories in `SnowRunner.exe` **only if the executable SHA-256 matches the existing inspected-build fingerprint**. A matching hash is an eligibility check, not a compatibility guarantee.

The probe does not draw anything in SnowRunner and does not read game memory. It writes bounded counters (factory creation, adapter enumeration, swap creation requests/results, swap-interface negotiation, Present, resize, unsupported interface requests and release) to `%LOCALAPPDATA%/SnowRunnerMinimap/bootstrap.log`, at most 1 MiB. No raw addresses, pose or saves are logged and no network requests occur.

**No deployment occurs during build or test.** A controlled startup test adds only this original DLL to the game's executable directory when no `dxgi.dll` already exists. It requires the user's go-ahead for a normal game exit/restart; preserve a fresh save backup first. The current session must not be killed or restarted automatically. Check startup, factory/swap/Present counters, normal exit and Steam overlay coexistence before enabling game rendering. If the game cannot start or interface compatibility fails, stop the attempt; with the game closed, move only the hash-verified added DLL out of its directory. Never overwrite another mod or remove an unrecognized DLL.

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

## Device ancestry regression and next implementation boundary

The owned host now also uses the documented `ID3D11Device::QueryInterface(IDXGIDevice)` -> `GetAdapter` -> `GetParent(IDXGIFactory)` route. It creates and presents an actual WARP swap chain through that factory and verifies that the export-only bridge's swap/Present counters do not change. This reproduces a concrete coverage gap: wrapping exported factory results does not intercept every factory obtained through a device.

An additional `D3D11CreateDeviceAndSwapChain` export wrapper alone would not cover applications that call `D3D11CreateDevice` and obtain a factory through device ancestry. A partial device facade is also insufficient: context/resource `GetDevice` calls would expose the original device, violating identity expectations unless the related object graph is handled consistently. Unknown-interface pass-through must not be treated as proof of consistent COM identity.

A complete next implementation needs either an identity-preserving device/adapter/factory graph, including context/resource return paths and native-call unwrapping, or an independently reviewed render interception route with established lifetime and compatibility. Neither is implemented here. The original export-only target does not mutate native dispatch pointers. The separately described opt-in instance route changes that design explicitly; it does not relax the launcher guard, and its owned tests do not establish game compatibility.

Primary API contracts: [device adapter](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgidevice-getadapter), [DXGI parent](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiobject-getparent), [device creation](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-d3d11createdevice).

## Instance-local factory interception experiment

Opt in with `-DSRMM_BUILD_FACTORY_ROUTE=ON`. This produces `factory-route/Release/dxgi.dll`, `d3d11.dll` and an owned test host. It supersedes the export-only route only for this separate target; the original launcher/guard and default targets are unchanged.

The D3D11 startup bridge calls the absolute installed system `D3D11CreateDevice`, then follows the real device's DXGI adapter/parent. It preserves native device, context, adapter and resource identities. The DXGI bridge checks factory7 interface-address consistency, writable instance storage and system-module ownership of the original table. It atomically replaces that factory instance's dispatch pointer with an original table; it does **not** overwrite the shared system table, Steam code, executable bytes or an import slot. This per-instance mutation is an experimental ABI-level interception, not a Microsoft-supported extension API.

Every factory method consults the retained system table afresh and delegates with the original object address. Four creation methods wrap the returned swap chain. The existing swap wrapper delegates presentation normally, so it does not copy a Steam trampoline. Device/context/resource identity and the previously missed native ancestry route are tested on the actual WARP runtime. All untouched D3D11 exports are checked by name and ordinal against the installed system library.

Factories, original system modules, registry and dispatch storage remain resident until process exit. Registration is idempotent and capped at 64 factory instances; extra factories remain untouched. The deliberate retained references avoid final-release/reclamation races. No hot-unload or factory-pointer restoration is attempted during play. This bounded retention and per-instance pointer mutation are material tradeoffs. Normal exit is required before removing the two added files. Every addition/removal must check expected hashes and refuse an existing unrecognized DLL.

The owned tests do not establish live Steam overlay behavior, compatibility with other mods, or actual game render-thread scheduling. Both binaries are specific to the local Windows export layout and exact eligible game fingerprint. Game rendering remains disabled in this diagnostic build. A controlled live test must first establish creation/Present counters and normal shutdown while preserving Steam overlay and security settings.

## Offline map and diagnostic telemetry integration

`SharedRenderer::initialize_dds` now accepts the bounded single-mip BC3 DDS emitted by the local map importer. Header flags, dimensions, mip count, block byte count, reserved fields and exact file size are checked before GPU creation. The renderer ignores unvalidated alpha. Invalid replacement data clears the renderer so it cannot silently keep displaying the previous map. It performs no filesystem, game-memory or network access.

`candidate_packet.*` consumes a fixed 256-byte little-endian diagnostic packet: magic/version/size, availability, PID, sequence, Windows `GetTickCount64` sample time, publisher session, canonical map identifier, executable digest, position and direction, then zero reserved bytes. It rejects malformed/nonfinite data, context/build/session mismatch, future or >750ms-old samples, older/rewritten sequences and clock rollback. Identical packets can redraw while fresh. Clock rollback poisons the gate until reconstruction. This binary feed is local data, not authentication or a trusted game API.

Calibration explicitly supplies six affine coefficients from world X/Z to north-up U/V plus a measured UV-per-pixel scale. Direction passes through the same affine basis before clockwise heading is computed. Storage-V flip is independent. Missing calibration validation, a singular/nonfinite transform, out-of-map position or unusable direction hides output. The current scalar UV scale targets square-map views; general independent axis scaling is not implemented. There is **no default game calibration**. Setting `validated` requires measured evidence; a synthetic test setting it does not validate the installed map.

The existing production `FrameController` still rejects native candidates. The new gate reports only `ready_diagnostic` and is linked to owned tests, not enabled game drawing. An owned host now exercises packet -> affine projection/heading -> BC3 texture -> actual hidden swap-chain pixels, including stale-frame suppression.

`tools/candidate_feed.py` prepares an explicit read-only publisher for a later authorized test. It accepts a PID, prior complete discovery report, canonical map ID and new output directory. It verifies the executable fingerprint, reuses only unique bounded root RVAs, rechecks RTTI and both root chains, and performs no discovery scan. It publishes at most 10 Hz for 1..600 seconds through atomic replacement of a 256-byte local file. A fresh random session accompanies each run; shutdown writes unavailable. Sample time is taken before the reads. A killed publisher expires through the consumer's freshness limit. Do not run it while game access is paused.

```powershell
# Later authorized diagnostic session only; paths and PID must be supplied explicitly.
python tools/candidate_feed.py --pid <PID> --layout <private-discovery.json> --map <level_id> --output-dir <new-private-directory> --seconds 30
```

The publisher's data is still a candidate. Stable roots do not make multi-field reads atomic and no gameplay/menu/garage/loading signal exists yet. Map changes are rejected by the consumer's explicit context. The session manifest and packet are private local files and must not be committed. Reading the packet and map on a background worker, connecting that worker to the actual game render route, deciding active HUD state, measuring calibration and validating movement/heading remain future integration work. No game is contacted by the unit tests or build.


Packet layout (all integers/floats little-endian; total exactly 256 bytes):

| Offset | Data |
| --- | --- |
| 0 | 8-byte `SRMMTEL1` magic |
| 8 / 12 / 16 / 20 | uint32 version=1, size=256, availability=0/1, PID |
| 24 / 32 / 40 | uint64 sequence, sample GetTickCount64, publisher session |
| 48 | 64-byte ASCII canonical level name, NUL and zero padding |
| 112 | 32 raw executable SHA-256 bytes |
| 144 / 168 | Three float64 position XYZ / direction XYZ values |
| 192 | 64 zero reserved bytes |

Future Windows readers must allow delete sharing while briefly reading the atomically replaced packet file; they must reject any size other than 256 and never extrapolate from an expired packet. Opening the manifest/packet does not itself authorize drawing a native candidate. The Python-to-C++ CTest checks full-width session IDs and oversize rejection using original fixture data.

For a later offline check of an explicitly supplied private texture copy, `srmm_alignment_host --local-map <map.dds>` reads a bounded file, validates it, and renders it in the hidden owned WARP host. It reads no game process and does not modify the installation. Texture upload success does not establish world calibration or live integration.


## Background ingestion and render-thread handoff

`SrmmFileIngestor` owns explicit file ingestion for an immutable configuration. It accepts absolute local fixed-drive paths, an expected map SHA-256, bounded poll/read deadlines, and a new generation. Start is explicit and must occur outside `DllMain` and graphics callbacks. It performs no game-process access and writes no input files. The map is read once per generation, validated and hash-bound before publication. Reconfiguring map/calibration/session requires a fresh ingestor and view; a map-ID change cannot silently reuse the previous texture.

The worker reads packets with delete sharing so the publisher can atomically replace them. Reads have size limits and overlapped completion waits. Missing/malformed input publishes unavailable state immediately upon observation; render-time freshness also hides stalled publishers. Immutable snapshots carry one complete packet and a shared map asset. Only the worker loads files and assembles published snapshots. `SrmmDiagnosticView` consumes snapshots on its construction/render thread, checks generation, map digest, calibration/context and current freshness, then creates/releases GPU resources on that same thread. It reinitializes for device identity changes, keeps no backbuffer reference between calls and requires an explicit HUD-allowed input from the caller. It does not discover a game's HUD state.

Stop revokes a shared admission token before signaling the worker, so even a retained old snapshot cannot begin a new draw. An already admitted render call may finish; its owner must drain render calls before destroying the view. Worker and control publication share a short mutex with no file I/O inside it; the render thread uses only atomic shared-snapshot loads. Stop waits belong to the control thread and return `false` for pending completion. There is no forced thread termination or hot-unload.

Windows cancellation is not completion. After `CancelIoEx`, the worker retains its OVERLAPPED structure, buffer and handles until the I/O completes. A slow driver can therefore keep a worker pending beyond the requested deadline. Its state is owned by the worker and its own code module is pinned for process lifetime before startup. Destruction requests stop, waits at most 250ms for the thread, and closes only the owner's thread handle; it cannot free pending worker storage. This deliberately trades possible retained memory/code until I/O completion or process exit for avoiding use-after-free. The pending-driver-hang case has not been forced in a real filesystem test. [Windows cancellation contract](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex), [asynchronous read buffer lifetime](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile), [module pinning](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandleexw).

`owned_background_ingestion` exercises real atomic file replacement, absent/malformed/oversized packets, digest and DDS failure, immutable snapshots, actual map/marker pixels, HUD hide, wrong-thread refusal, wrong generation/map, resize/device replacement, render-time expiry, revocation of retained snapshots, duplicate/pre-start stop and cooperative drain. All files are original temporary fixtures and its WARP window stays hidden. The new worker/view are currently linked only into this owned host, not into automatic game startup. Native callback routing and HUD visibility still need controlled live verification before game activation. No additional fixture repetition can supply that evidence.
