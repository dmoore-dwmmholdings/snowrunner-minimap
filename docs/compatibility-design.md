# Proposed compatibility design and decision

2026-11-03. **Design proposal, not an implemented or game-validated adapter.** The existing launcher guard remains unchanged. This document separates a workable ownership model from the missing means of entering SnowRunner safely.

## Decision

Use explicit host registration and a module that stays loaded until process exit. Disabling the minimap stops its work and releases resources after callbacks finish; it does not unload executable code during play. Uninstall would occur after a normal game exit. This removes hot unloading as a design requirement, without assuming ownership of another component's hooks.

The unavailable piece is an actual SnowRunner host interface providing the registration and lifecycle below. Our own executable can provide it; the game has not been shown to. Replacing that interface with interception of Steam, system functions or a game swap chain would reintroduce the compatibility problem. This proposal does not authorize such substitution.

## Proposed interface and ownership

These are original interface requirements, not names of discovered game functions:

| Owner | Required responsibility |
| --- | --- |
| Host/bootstrap | Load the exact original module through a reviewed route; retain its load reference for the process lifetime; call initialization outside `DllMain`; supply a versioned interface and an explicit supported-build/capability result. A fingerprint identifies a build but cannot establish compatibility by itself. |
| Host dispatcher | Own callback registrations. Serialize registration/revocation and callback admission. Return an opaque registration token and render-generation value. Revoke prevents new admissions; a separate drain result reports when admitted callbacks have returned. |
| Plugin | Own its session, immutable telemetry snapshot and graphics resources. Keep a minimal resident dispatcher. Never store a foreign trampoline or rewrite a callback slot owned by another component. |
| Render host | Call on the correct immediate-context thread at a documented HUD phase, bracket resize/device changes, and identify the current device/backbuffer generation. Supply a way to observe completion of graphics work that still uses plugin resources. |
| Telemetry adapter | Publish coherent values with build, session, map, vehicle, sequence and monotonic timestamp. Object pointers never enter render packets. Publish explicit unavailability on transitions or failed reads. |

The host retains its reference instead of pinning any foreign module. Windows reference-count APIs support module lifetime, but do not control another component's hook storage or shutdown. The design therefore avoids relying on foreign hook lifetime. [Microsoft module-reference documentation](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandleexw)

## State transitions

| State / event | Action and completion rule |
| --- | --- |
| Loaded, disabled | No session or rendering callback is active. Negotiate version, structure sizes, supported build and required capabilities. Failure leaves the module inert and resident. |
| Start requested | Create session/resources before exposing a registration token. Publish Active only after registration succeeds. Roll back owned resources on failure; do not claim initialization from a once flag alone. |
| Active frame | Host admits the callback under its registration lock and increments its in-flight count, then releases the lock. Callback checks token/generation and snapshot validity. Rendering is synchronous on the host render thread; no worker uses the immediate context. On return the host decrements its count. |
| Resize/device transition | Stop drawing for the old generation. Release plugin backbuffer views through the host's pre-resize phase after prior admitted work is done. Recreate resources only after the host supplies a valid new generation. Failed recreation remains hidden. |
| Disable requested | Publish Stopping, clear the pending render packet, request callback revocation, and stop telemetry publication. Previously presented pixels disappear when the host repaints; do not modify its backbuffer from the control thread. Never wait for callback drain from inside that callback or from `DllMain`. |
| Drain completed | Once no callback or telemetry operation can use the session, destroy it. Release graphics resources only when the host's lifetime/completion rules allow. Clear borrowed API pointers. Enter Disabled; module code remains mapped. |
| Drain timeout or ambiguous completion | Stay disabled/pending. Keep potentially referenced code and data alive. Do not retry initialization, force-stop a thread, free pending storage or restore another participant's pointer. Reconcile only a known completion. Normal process exit remains the final cleanup boundary. |

Configuration changes use a new generation; they do not reuse an old callback token. Thread-affine graphics cleanup is scheduled by the host on its render thread, not performed by the control thread. The existing ABI's create/update/destroy functions can supply the session boundary, but this host registration protocol has not been implemented.

## Rendering contract

The render callback requires exclusive use of the immediate context for its duration. Direct3D documents context access as single-threaded; adding a plugin-local mutex cannot serialize the game's unrelated use. [Microsoft threading guidance](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-intro)

For a host exposing Direct3D 11.1 context-state interfaces, use a plugin context-state object and restore the previous object on every return path. Query the required interfaces and decline that backend if unavailable; do not silently assume a partial state backup is adequate. `SwapDeviceContextState` is documented for plugin-style state preservation, but it does not schedule the callback, serialize another thread or undo writes to resources. It is not supported on deferred contexts. [Microsoft state switching](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11devicecontext1-swapdevicecontextstate)

Only the agreed output region may be changed. Do not call `ClearState` on the game's context as a cleanup shortcut. The host must ensure all direct and indirect backbuffer references are released before `ResizeBuffers`, including references retained by state objects or command lists. Releasing only our visible render-target pointer is insufficient. The offscreen renderer's resize tests do not validate this game-host obligation. [Microsoft resize contract](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-resizebuffers)

## Alternatives and what can actually be validated

| Path | Documented foundation | Safe validation available now | Remaining gap / consequence |
| --- | --- | --- | --- |
| Cooperative in-game HUD host | Windows DLL and Direct3D APIs support the components; reviewed Saber pages do not expose the required game host interface. | Existing owned DLL/session and renderer tests cover component behavior. A host-owned test callback could validate more implementation details, not its existence in SnowRunner. | Native bootstrap, callback scheduling, revocation and resize notification are unavailable. No deployable native path established. |
| Community proxy/ASI host | Public source and a separately licensed loader exist; these are not a publisher runtime API. | Completed read-only source and lifecycle review. | Import modifications, foreign-chain ownership and lifecycle assumptions remain unresolved. No installation or guard workaround proposed. |
| Separate transparent Windows window | `UpdateLayeredWindow` documents alpha-composited window content. | Can be built and tested over an owned window without touching the game. | External overlay rather than the requested native mod; fullscreen/DPI/focus and real telemetry still need validation. Requires a deliberate scope change before implementing this product. |
| Screen-derived position | Windows Graphics Capture provides window/display frames through a user-selected capture surface. | Capture/recognition can be evaluated on an owned test image. | No evidence of continuous map position when the game's map is closed. Capturing pixels does not create a missing position signal. |
| Read-only memory candidate | `ReadProcessMemory` is a documented read API. The existing project probe treats decoded pose as a candidate. | Bounded reads and malformed-input handling are testable; live heading, movement, vehicle identity and transitions need controlled game observations. | Memory copying is not a documented game telemetry schema or atomic snapshot of several game objects. It does not supply a renderer or solve bootstrap. |
| Save/manual snapshot | Ordinary file/manual input. | Parser behavior on authorized fixtures can be tested. | No verified live cadence, schema or continuous heading feed; insufficient for a live minimap. |

Sources: [Windows layered-window API](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-updatelayeredwindow), [Windows Graphics Capture](https://learn.microsoft.com/en-us/windows/apps/develop/media-authoring-processing/screen-capture), [ReadProcessMemory](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-readprocessmemory), [Saber sample mod workflow](https://expeditions-guides.saber.games/truck_modding/getting_started/sample_mod_by_the_game/overview/), [Saber gameplay entities](https://expeditions-guides.saber.games/custom_gameplay_entities/custom_gameplay_entities_intro/). Assessments in the final column are engineering inferences, not claims that the OS APIs establish SnowRunner support. The documentation search is bounded; it does not prove no private extension interface exists.

## Risk review and stopping condition

The proposed process-lifetime policy resolves **our code being unloaded while a callback can still execute**. It does not resolve a foreign trampoline disappearing, another component replacing an import, a callback on the wrong thread, a stale game pointer or a resource held across resize. Those are addressed only by the specified cooperative host and telemetry contracts. A disk signature, matching bytes at one instant, or an additional owned fixture is not that contract.

This pass performed a separate counterexample review against each ownership assumption: concurrent callback admission during disable; a callback requesting its own shutdown; late completion after timeout; a device generation change; a second participant replacing an entry; and an invalid telemetry snapshot. The prescribed responses are revocation plus drain, asynchronous stop, retention until known completion, generation rejection, no foreign-slot writes, and hidden output respectively. This is design analysis by the same author, not an independent third-party certification or runtime validation.

**Current boundary:** no concrete native operation can be proposed under the present constraints until an actual bootstrap and host callback/lifetime interface is established. Process-lifetime residency is a viable design choice, but does not make the current guarded loader compatible. Do not restart the same loader research loop or create another generic fixture to imply progress on that missing interface.

No additional user permission is needed for the design. An external-window implementation would be a genuine scope choice, but prior direction retains the native mod, so it has not been selected or built. The useful external input would be publisher/host documentation exposing registration, rendering, resize and callback revocation, not a blanket risk waiver. No outreach was sent. This pass changes documentation only; no tests were rerun, no game access occurred and no compatibility claim was promoted.
