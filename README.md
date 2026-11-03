# SnowRunner Minimap

An early Windows minimap mod project, developed on macOS for later Windows testing.

**Current state:** original Windows launcher and bounded read-only pose probe, plus the isolated diagnostic DLL/host and [offscreen Direct3D renderer](docs/mock-renderer.md). Debug and Release pass 7/7 CTest cases, including 56 rendered-pixel/lifecycle checks and 66 [owned hook-lifecycle assertions](docs/hook-lifecycle-fixture.md); the earlier 32 Python tests pass. A real parked Scout produces consistent pose candidates and a matching map ID, but heading/movement are unverified. The native load attempt stopped at a loader compatibility guard before modifying the process. There is no working in-game minimap yet. See the [live validation checkpoint](docs/live-validation.md). The [native loading decision](docs/loading-decision.md) records the unresolved compatibility contract and the evidence needed to resume; further injection attempts are paused. See the [community loader review and renderer lifecycle update](docs/community-loader-review.md). The [proposed compatibility design](docs/compatibility-design.md) defines process-lifetime ownership, callback shutdown and the remaining host-interface boundary.

[Live Andare board](https://andare.bottega-suite.com/w/RLdpCCh1YgoQVD5lYRoU/p/SNOW/board) · [Feasibility](docs/feasibility.md) · [Native research and boundaries](docs/native-integration.md) · [Windows handoff](docs/windows-testing.md)

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
