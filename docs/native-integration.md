# Native in-game research and adapter boundary

Updated 2026-11-03. The target remains an in-game Windows minimap. The Windows checkpoint now includes a successful MSVC build, read-only executable inspection, normal game launch, display-setting observation and process module enumeration. No game memory has been scanned, debugger attached or native mod loaded. See [Windows evidence and the REA analysis timeout](windows-inspection.md).

## Precedent and version limits

Read-only source review used the original [ElAmine01 source commit](https://github.com/ElAmine01/Minimap-For-SnowRunner-Source-Code/commit/fded36a6126b199d2d3253968a3e9cc5ca1ef8e9) and [ddarbs fork commit](https://github.com/ddarbs/Minimap-For-SnowRunner-Source-Code/commit/05b5fe776b3e833dbf35aa3c09d6ad59c98d4a01). Neither was built or executed.

- The original [CMake file](https://github.com/ElAmine01/Minimap-For-SnowRunner-Source-Code/blob/fded36a6126b199d2d3253968a3e9cc5ca1ef8e9/CMakeLists.txt) targets x64 Windows DLLs, Win32/DX11 rendering, and separately supplied dependencies. It is not a portable game integration we can validate with Apple Clang.
- Its [DXGI rendering source](https://github.com/ElAmine01/Minimap-For-SnowRunner-Source-Code/blob/fded36a6126b199d2d3253968a3e9cc5ca1ef8e9/src/asi/hooks/DXGIHook.cpp) connects drawing to presentation and resize operations. This is evidence of an in-process approach, not proof of compatibility with our game, graphics stack or other mods.
- The [maintenance notes](https://github.com/ElAmine01/Minimap-For-SnowRunner-Source-Code/blob/fded36a6126b199d2d3253968a3e9cc5ca1ef8e9/MAINTENANCE.md) describe measurements dated 2026-08-22 and distinguish address relocation from internal-layout changes. They acknowledge the latter may require code changes. They do not provide a verified compatibility result for Dawson's installation. We adopt no pointer values or universal map scale from them.
- [Saber's troubleshooting guidance](https://support.saber.games/hc/en/snowrunner/articles/snow-runner-mods-issues-9) recognizes that older or incompatible mods can fail. Every storefront/build/graphics configuration is therefore unverified here; a recent source commit alone does not establish compatibility.

## Reuse, game terms and protection uncertainty

The separate [binary distribution LICENSE](https://github.com/ElAmine01/Minimap-For-SnowRunner/blob/main/LICENSE) is MIT. At the source commits above, GitHub metadata reported no detected license and the complete file trees contained no root LICENSE/COPYING file; the reviewed README did not grant a source license. A different repository's license is not treated as established permission for all source contributions, dependencies or map images. No source was copied. Our contracts, tests and demo are original. [GitHub's licensing guidance](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/licensing-a-repository) distinguishes source visibility from reuse permissions.

Saber's [mod approval policy](https://expeditions-guides.saber.games/mod_approval_policy/) documents a content verification process; it does not establish permission for this particular injected runtime implementation. Steam is now verified as the target storefront. The [Steam-linked EULA](https://store.steampowered.com//eula/1465360_eula_0) was retrieved during the [pose/loading follow-up](pose-and-loading.md); Article 3 includes reverse-engineering/modification restrictions absent publisher consent. The currently served text does not identify the version accepted on this installation or settle applicable exceptions. No project-specific publisher permission, legality, anti-cheat presence/absence, multiplayer acceptance, Denuvo behavior or account-safety conclusion is established. Do not bypass protection, adopt third-party "false positive" assurances, or change antivirus settings.

## Implemented contract

`TelemetrySource::poll()` returns a value snapshot or no new event. `TelemetrySample` carries a canonical exact build manifest, source kind, session/map/vehicle keys, sequence number, monotonic sample time, availability and optional normalized pose. No memory layout is embedded in this contract.

`FrameController` accepts synthetic input only. It compares every manifest field, rejects mismatched context and repeated/out-of-order samples, and clears geometry on invalid/future/stale data, read failure, menus, garages, loading or disconnect. A frame can be redrawn while a cached sample remains fresh. New context clears the old cache and sequence scope. Clock rollback clears the cache. Invalid calibration is rejected. Ready output carries its map/session/vehicle key, calibrated view and marker heading; its status explicitly says synthetic. A native candidate always produces unavailable output, even with a matching manifest.

This is a single-threaded contract, not a concurrency mechanism. A future native worker must hand off coherent immutable snapshots; the renderer must not independently combine partially read pose and map fields. No interpolation, dead reckoning, automatic memory scan, loader, graphics hook or installer exists.

## Required before actual integration

1. Windows build access, Steam build identity and executable SHA-256/architecture are now verified; borderless display mode was observed. Displayed game version and active graphics API remain unresolved. The tested identity collector reads metadata only and does not launch the game.
2. Review the applicable installed terms and relevant protection/online-mode policy for the intended test setting. Establish dependency/source rights if any reuse is proposed.
3. The user subsequently authorized REA to inspect the game on Windows. Scoped read-only inspection may proceed. DLL loading, injection, hooks and game-directory modifications still require explicit approval of a concrete proposal; no such operation has been performed.
4. User-local map imagery with permission to use it, plus independent calibration measurements. Do not publish game assets or adopt a remote CDN by assumption.
5. On the exact Windows build, validate map identity, vehicle position and heading conventions, coherent reads, timing, transitions, graphics state preservation, resize/device lifecycle, input focus, shutdown and uninstall. A build fingerprint is an identifier, not evidence these tests passed.

Until these inputs and tests exist, SNOW-4/5/6 remain unfinished. No overlay choice is required from the user.
