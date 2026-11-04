# SnowRunner Minimap

An experimental native Windows minimap, developed and tested locally. An earlier
build rendered the real map and truck position in SnowRunner; the user confirmed
the heading and smoothing updates during test drives. Full game-state and
transition acceptance is still incomplete.

The latest combined implementation adds a game-bound feed launcher, automatic
local map discovery/import, map-generation switching with rectangular-map
geometry, saved-destination edge markers, and an in-game F10 display panel with
persistent zoom/tracking/shape/size/location settings. See the
[combined checkpoint and coordinated test plan](docs/combined-checkpoint.md).

**Limits:** this combined build has not been installed or live-tested. Automatic
menu detection still lacks a verified runtime source. Destinations are saved
snapshots, not live mission events; cross-map gateway routing is not implemented.
Imported map calibration stays visibly provisional until landmark checks pass.
No game assets, private recordings, credentials or third-party binaries are
published. No paid CI workflows are used.

The current Windows implementation lives in [experimental/dxgi](experimental/dxgi/README.md).
Earlier research/loading documents record historical boundaries; they do not
describe the current feature set. The separate portable core remains a synthetic
geometry/adapter testbed.

[Andare board](https://andare.bottega-suite.com/w/RLdpCCh1YgoQVD5lYRoU/p/SNOW/board) |
[Local feed launcher](docs/feed-lifecycle.md) |
[Map import](docs/map-import.md) |
[Native settings](docs/native-display-panel.md)

## Run locally

Requires CMake 3.20+ and a C++17 compiler. No third-party dependencies or downloads in the build.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/minimap_demo > build/synthetic-preview.svg
```

Open the SVG in a browser. It shows four heading-up replay samples on original synthetic terrain. This validates geometry only, not the SnowRunner HUD, game telemetry or Windows rendering.

The core supports affine calibration from three landmarks, map-image projection, cardinal heading rotation, zoom, circular bounds and telemetry freshness classification. Its internal world convention is east/north and is explicitly **not** a verified SnowRunner coordinate convention. The future game adapter must convert and verify it.

The demo runs through the same telemetry/frame contract used in the mock tests. Samples carry exact build identity and session/map/vehicle identifiers. Invalid, stale, mismatched or failed reads clear the marker; a native candidate is always rejected in the current prototype. The separate experimental Windows tools now implement read-only candidate discovery and guarded diagnostic loading; they do not enable native frames in the core. The Mac checkpoint passed 27 geometry checks plus 48 adapter checks. Windows adds PE, diagnostic DLL and launcher lifecycle tests.

## Plan

1. SNOW-2: evidence-backed feasibility review.
2. SNOW-3: shared geometry and synthetic replay prototype.
3. SNOW-7: mock-driven native telemetry and frame contracts.
4. SNOW-4: validate native in-game integration and live pose on Windows.
5. SNOW-5: user-local map import and measured alignment.
6. SNOW-6: local Windows build, runtime validation and uninstall instructions.

No paid CI workflows, game assets, credentials, executable downloads or mod-marketplace publication are included.

## Offline script investigation

The Windows installation contains a compiled script bundle with bounded minimap and truck modules. The [offline inspection report](docs/script-inspection.md) and [pose/loading assessment](docs/pose-and-loading.md) distinguish observed references from inferred declarations and record the unresolved loading boundary. The original Python inspectors read framing and metadata without executing scripts or emitting bytecode; all 23 synthetic inspection tests pass. This is research tooling, not a game adapter or installer.
