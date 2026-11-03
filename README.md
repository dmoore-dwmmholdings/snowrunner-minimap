# SnowRunner Minimap

An early Windows minimap mod project, developed on macOS for later Windows testing.

**Current state:** native in-game mod research, portable geometry and mock-driven adapter prototype. The Windows x64 build passes 86 checks (75 C++ checks and 11 PE inspection checks). There is no live SnowRunner connection or working in-game mod yet; game runtime validation remains pending. See the [Windows inspection checkpoint](docs/windows-inspection.md).

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

The demo runs through the same telemetry/frame contract used in the mock tests. Samples carry exact build identity and session/map/vehicle identifiers. Invalid, stale, mismatched or failed reads clear the marker; a native candidate is always rejected in the current prototype. No process-access or injection implementation is included. The Mac checkpoint passed 27 geometry checks plus 48 adapter checks. Windows adds a PowerShell PE inspection test, with 3/3 CTest cases passing on MSVC.

## Plan

1. SNOW-2: evidence-backed feasibility review.
2. SNOW-3: shared geometry and synthetic replay prototype.
3. SNOW-7: mock-driven native telemetry and frame contracts.
4. SNOW-4: validate native in-game integration and live pose on Windows.
5. SNOW-5: user-local map import and measured alignment.
6. SNOW-6: local Windows build, runtime validation and uninstall instructions.

No paid CI workflows, game assets, credentials, executable downloads or mod-marketplace publication are included.
