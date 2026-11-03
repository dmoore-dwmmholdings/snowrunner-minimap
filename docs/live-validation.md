# Live validation checkpoint — 2026-11-03

The Windows tools can read plausible stationary pose data from the installed game. They do not yet render a minimap. The native launcher is implemented and passes an isolated remote load/API/unload test, but its first game load attempt stopped at its own compatibility guard before any remote allocation, write or thread creation.

## Exact scope and observed results

Target: Steam build 26218478, product 1.907887.SNOW_DLC_19, x64 executable SHA256 `d46623b23808d19a40f7ebeec15332d11f97317637ec4d4e072a501bc78a77c8`. The launcher and probe refuse other executable hashes. This is one build and one session, not general compatibility.

Before continuing the existing single-player save, 29 Steam save files were copied into a private archive outside the repository and each archive entry was verified against its source SHA256. No save was edited or restored. The existing save loaded Smithville Dam into an empty garage slot. An owned Scout 800 was deployed from storage and placed outside the garage through the normal game UI, engine off and handbrake engaged. No driving inputs, purchases, customization, settings changes, recovery, or save restoration were performed during this checkpoint.

| Observed game state | Bounded discovery | Samples |
| --- | --- | --- |
| Main menu | Complete scan: no truck or level candidate | None |
| Loaded Smithville Dam, empty garage | One truck-control RTTI match; one `level_us_01_02` candidate | Three null poses |
| Scout parked outside garage | Same unique truck-control and level roots | Ten consistent finite pose candidates |

The parked position candidate was approximately `(513.279, 115.532, 894.420)`. Its orientation candidate was approximately `(-0.045516, 0.001600, 0.998962)`. These are raw candidate XYZ values, not verified map coordinates or heading. The level identifier agrees with the visible Smithville Dam UI. Stationary agreement does not prove current-vehicle identity, units, axis orientation, freshness during movement, transition handling or a heading formula. The production `FrameController` still rejects native candidates.

The initial 8,192-pointer diagnostic cap was insufficient for the observed roughly 19,300 pointers. It was raised to a fixed 32,768; excess candidates or unreadable data pages still disable sampling. Successful discovery examined the bounded executable `.data` section and only referenced objects, not arbitrary heap ranges. Private reports are ignored under `build/live-pose-candidates-*.local.json`.

The process contained DXGI and D3D11 modules in this snapshot. Presence alone does not prove the active render path; no swap chain or graphics callback has been observed.

## Tools and reproduction

Build with the existing Windows x64 CMake configuration. `srmm_launcher.exe`, `srmm_fixture_target.exe` and `srmm_diagnostic.dll` are adjacent under `build/Release` (or Debug). Use only a reviewed local build; the adjacent DLL is not an authenticated plugin-distribution mechanism.

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python -m unittest discover -s tests -p 'test_*.py'
.\build\Release\srmm_launcher.exe --self-test
# Replace <PID> with the current, independently verified SnowRunner PID:
.\build\Release\srmm_launcher.exe --inspect <PID>
python tools\probe_live_pose.py --pid <PID> --samples 10 --output build\new-probe.local.json
```

`--self-test` creates our own hidden fixture, loads the diagnostic DLL into that child, exercises one synthetic API frame, destroys the session, unloads, and verifies child exit. Only its owned fixture may be terminated on failure. It never targets the game.

`--inspect` uses query/read rights only. It verifies executable identity/architecture, lists relevant modules, checks whether the diagnostic is present, and reports loader-function compatibility. Exit zero means inspection completed; read the compatibility lines rather than treating zero as permission to inject.

The Python probe uses query/read rights only, `VirtualQueryEx` and bounded `ReadProcessMemory`; it skips guard, inaccessible, execute-only and uncommitted regions. It verifies the executable hash and PE bounds. Sampling requires exactly one RTTI-checked truck-control root and one level-name candidate. Pointer-chain and identity rechecks reduce races but are not a game lock. No debugger, game writes, privileges, hooks, protection changes or network operations are implemented. Reports must be new files outside the installation. Layout hypotheses were informed by the pinned community [maintenance notes](https://github.com/ElAmine01/Minimap-For-SnowRunner-Source-Code/blob/fded36a6126b199d2d3253968a3e9cc5ca1ef8e9/MAINTENANCE.md); the implementation is original and its observations do not adopt that project's patch-stability or heading claims.

## Native loading result and boundary

`--diagnostic-load <PID>` is an explicit experimental mode, not a minimap launcher. It requests process create-thread/VM operation/write rights, allocates a read/write buffer for the absolute adjacent DLL path, calls the target's `LoadLibraryW`, finds the full-width module handle, then calls `FreeLibrary` and verifies absence. It installs no files. The current DLL has no automatic worker, hook, game API call or custom DllMain. Remote dependency resolution uses the target's normal loader rules; this differs from the isolated host's restricted `LoadLibraryExW` search flags. Runtime incompatibility could crash the game.

Before any remote call, the launcher matches the actual local export-owner module by path and image size, calculates its RVA, and requires the first 32 bytes to match. It never assumes equal ASLR addresses or truncates a 64-bit module handle to a thread exit code. Existing diagnostic modules are refused because reference ownership is unknown. A remote thread timeout is explicitly uncertain: no retry/unload or game termination occurs, and its path buffer stays allocated until target exit. Do not blindly retry an uncertain result.

The authorized game attempt returned exit 1: `System export bytes differ; refusing remote call.` A subsequent **read-only** inspection established:

- `KERNEL32.DLL!LoadLibraryW`, RVA `0x3ee30`, differs starting at byte zero.
- `KERNEL32.DLL!FreeLibrary`, RVA `0x3bd70`, also differs starting at byte zero.
- Both target entries begin with relative jumps into memory outside the enumerated modules. A bounded, single-hop attribution check could not identify an owner; it did not follow or execute the destinations.
- Our diagnostic DLL is absent. The game remained responsive with the Scout parked.

The mismatch's cause is unknown. This is our application's guard, not an automatic approval denial or a demonstrated OS/anti-cheat block. No allocation, process write, remote thread, DLL load or unload occurred in the game. No injection retry, guard removal, hook removal, alternate loader route, security change or protection bypass was attempted.

The next native step is read-only attribution of the loader differences and a compatible reviewed load/lifecycle design. Do not bypass the mismatch to force a load. Independently, pose validation needs controlled movement/rotation, vehicle and garage/menu transitions, a second session and measured map calibration. Native rendering and user-local map imagery remain unimplemented. Neither an external overlay nor synthetic terrain has been substituted for a working in-game minimap.

## Verification and cleanup

MSVC x64 Debug and Release build successfully. Each passes five CTest cases: geometry/freshness, native adapter contract, isolated diagnostic plugin, isolated launcher injection and PE inspection. The diagnostic host still runs 258 assertions across three load/unload cycles; the launcher test is an additional synthetic end-to-end lifecycle check. All 32 Python tests pass, including nine pose-probe checks covering malformed RTTI/PE metadata, invalid values, chain changes, ambiguity/bounds and read-only access to an owned Python buffer.

There are no installed game files to remove, no mod DLL resident in the game, and no background minimap service. Build artifacts and private reports can be removed from the project after all owned test processes exit. Keep the save backup private; do not restore it over newer progress automatically. Game assets, screenshots, save files, raw memory dumps and third-party source are not committed.
