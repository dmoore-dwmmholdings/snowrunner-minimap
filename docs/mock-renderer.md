# Isolated Direct3D minimap renderer

`MockD3d11Renderer` renders the existing synthetic frame contract into an owned RGBA texture using original HLSL. It has no process access, hooks, game window, swap chain or dependency on game assets. This advances rendering tests without establishing an in-game HUD integration.

The renderer creates its own headless Direct3D 11 feature-level-11.0 device with `D3D_DRIVER_TYPE_WARP`. Microsoft's [WARP guide](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/directx-warp) describes this software rasterizer; the device is created through [D3D11CreateDevice](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-d3d11createdevice). No hardware performance or game graphics-API claim follows from these tests.

## Behavior

- Circular clipping, a centered directional marker, heading-up and north-up views, map translation and zoom.
- Original procedural terrain: grid, straight road and cyan landmark at synthetic image coordinate (300,250). There is no SnowRunner map image or terrain claim.
- Only `synthetic_ready` packets with bounded finite geometry render. The viewport must match the owned target; unsupported states and malformed geometry produce a fully transparent texture.
- Every call clears its target first, preventing cached map/marker pixels from surviving rejected telemetry. Texture readback respects the staging texture's row pitch.
- Device, context, shaders, buffers and textures use RAII. Each instance is single-threaded and owns its context; the interface cannot accept a game's device or context.

## Resource lifecycle

`resize(size)` accepts 64..2048 pixels and commits a complete replacement texture/view/readback set. Zero size or `suspend()` releases the owned device; rendering while suspended returns size zero and no pixels. Nonzero resize resumes using a newly created WARP device. Invalid dimensions preserve existing state. The caller serializes all operations, and geometry must match the current viewport. See the [community review and lifecycle scope](community-loader-review.md). Actual GPU removal and allocation failure were not forced in tests.

## Build and verification

The existing Windows x64 diagnostic build includes the renderer and `srmm_render_host.exe`. Only Windows SDK/runtime libraries are used; no dependency downloads are required.

```powershell
cmake --build build --config Release
.\build\Release\srmm_render_host.exe
ctest --test-dir build -C Release --output-on-failure
```

The headless host runs **56 assertions against rendered pixel buffers, frame transitions and resource lifecycle**: target size, corner clipping, centered marker, landmark alignment, heading rotation, disappearance of old pixels, zoom, translation, north-up marker rotation, garage/menu/loading/disconnection/read-error clearing, stale-data clearing, invalid geometry, viewport mismatch, unsupported state with supplied geometry, and oversized-target refusal. It consumes the real `FrameController` with a synthetic source. Resize/suspend/resume cases also check dimensions, centered markers, landmark distance, old-viewport rejection, invalid-resize preservation, empty suspended output and pixel equality after device recreation. Debug and Release each pass all seven CTest cases, including this renderer test. No Python files changed in this update; the earlier 32 passing Python checks were not rerun.

## Remaining integration work

This is an offscreen software-rendering prototype, not a game-compatible renderer or external overlay. It has no text, local map-image import, antialiasing, automatic hardware device-loss detection/recovery, game swap-chain resize handling, compositing into a game backbuffer, graphics-state preservation for a shared context, or measured performance budget. GPU readback is used for correctness testing and should not become the production per-frame path. Native telemetry remains rejected by the core. Live heading/movement/calibration and a reviewed hook-preserving loading lifecycle are separate unresolved work.
