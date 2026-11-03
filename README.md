# SnowRunner Minimap

An early Windows minimap mod project, developed on macOS for later Windows testing.

**Current state:** research and portable geometry prototype. There is no live SnowRunner connection or working in-game mod yet. The integration decision is open; an external overlay has not been selected as a substitute.

[Live Andare board](https://andare.bottega-suite.com/w/RLdpCCh1YgoQVD5lYRoU/p/SNOW/board) · [Feasibility and sources](docs/feasibility.md) · [Windows handoff](docs/windows-testing.md)

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

## Plan

1. SNOW-2: evidence-backed feasibility review.
2. SNOW-3: shared geometry and synthetic replay prototype.
3. SNOW-4: choose integration path and validate live pose on Windows.
4. SNOW-5: user-local map import and measured alignment.
5. SNOW-6: local Windows build, runtime validation and uninstall instructions.

No paid CI workflows, game assets, credentials, executable downloads or mod-marketplace publication are included.
