# Feasibility checkpoint — 2026-11-03

## Conclusion

A true live minimap cannot currently be promised through an official SnowRunner SDK. In the reviewed public Saber documentation, no supported HUD-rendering extension or live vehicle telemetry API was found. That is a bounded research result, not proof that no private or undocumented capability exists.

An unofficial in-game route has community precedent, but it uses process integration outside the documented mod content system. Do not silently turn this project into an external companion. Product choice and Windows verification are still required.

## Evidence

- [Saber: About These Docs](https://expeditions-guides.saber.games/about_these_docs/) describes truck/map content and distinguishes Expeditions-only gameplay entities. This supports the documented content scope, not a general runtime SDK.
- [Saber: Custom Level Entities](https://expeditions-guides.saber.games/custom_level_entities/custom_level_entities/) lists content types such as models and terrain overlays. Here “overlay” means level content, not a desktop or HUD overlay.
- [Saber: Installation and First Launch](https://expeditions-guides.saber.games/map_modding/getting_started/installation_and_first_launch/) places the editor in the Windows installation and says Mac/Microsoft Store installations do not include it. We can build portable code on Mac, but cannot claim an editor-produced mod from this machine.
- [Saber: Introductory Info and Initial Objectives](https://expeditions-guides.saber.games/map_modding/creating_a_map/other_map_settings/introductory_info_and_initial_objectives/) exposes configured map popups/objectives; it does not establish an arbitrary live minimap drawing API.
- [Original community SnowMap distribution](https://github.com/ElAmine01/Minimap-For-SnowRunner) describes proxy-DLL injection and game-memory tracking. This establishes a claimed unofficial approach, not official support, safety, licensing for all bundled components, or compatibility with our target build.
- [Community source fork README](https://github.com/ddarbs/Minimap-For-SnowRunner-Source-Code) describes DX11 hooks, runtime memory discovery, map downloads and Windows tools. These remain author claims; no binaries were downloaded or run, no source copied, and no claimed offsets, patch immunity, coordinate scale or CDN asset rights were adopted. The fork is not treated as upstream authority.

Sources accessed 2026-11-03. Site-restricted searches for telemetry/API/HUD did not surface a supported runtime telemetry/HUD SDK. The guide is a starter pack, so absence is recorded as uncertainty. mod.io’s detailed page required JavaScript in the research tool and was not treated as fully inspected.

## Local observations

The current workspace was empty and had no inherited AGENTS.md in its parent chain. The default Steam library on this Mac contained no SnowRunner installation; other drives/libraries and the Windows machine have not been inspected. No SnowRunner game files, editor or save schema have been verified. CMake and Apple Clang are installed. No unknown software has been installed or executed.

## Candidate paths — decision pending

| Path | Display | Position and heading | Main unresolved evidence |
| --- | --- | --- | --- |
| Unofficial native mod | In-process Windows renderer | Version-specific game-memory discovery | Target game build, integration permission, dependencies/license review, robustness, game/anti-cheat compatibility |
| External Windows overlay | Separate transparent, topmost window, initially borderless game mode | Still requires a separately validated source; read-only process access is unofficial too | No official feed identified; render placement alone does not solve tracking; fullscreen, DPI and input behavior untested |
| Screen-derived companion | External window or second screen | Potential visual map-marker recognition when map UI is visible | No proof of continuous tracking with map closed; calibration/occlusion and capture behavior untested |
| Manual or save-snapshot companion | External map with user selection/snapshots | Manual pose or potentially delayed save-derived pose | Save schema/cadence/heading unknown; cannot represent this as a live minimap |

The portable geometry prototype is compatible with these options. No integration code has been chosen or installed. No anti-cheat compatibility or security exemption is claimed. Do not disable protection or request antivirus exclusions based on community assertions.

## Next evidence gate

1. Choose native research versus external overlay after reviewing this checkpoint.
2. On Windows, record storefront, game version, executable build, graphics mode and screen/DPI configuration.
3. Verify a position/heading source with timestamp, map identity and garage/transition handling on that exact build. Treat missing/stale data as unavailable, not interpolated truth.
4. Import permitted user-local map imagery; measure at least three noncollinear calibration landmarks plus independent check points. Never assume one scale for all maps.
5. Only then build the selected renderer and measure live alignment, latency, input and stability.
