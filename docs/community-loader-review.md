# Community loader review and original renderer lifecycle

Reviewed 2026-11-03. Public source was inspected as text only. No community binary, installer, dependency or source was built, executed or copied into this project.

## What the community minimap actually does

Review is pinned to [ElAmine01 source fded36a](https://github.com/ElAmine01/Minimap-For-SnowRunner-Source-Code/tree/fded36a6126b199d2d3253968a3e9cc5ca1ef8e9), rather than assuming the distributed binaries match that source.

- The [proxy entry point](https://github.com/ElAmine01/Minimap-For-SnowRunner-Source-Code/blob/fded36a6126b199d2d3253968a3e9cc5ca1ef8e9/src/proxy/dllmain.cpp) loads the system input library and the minimap ASI during process attach. The [plugin entry point](https://github.com/ElAmine01/Minimap-For-SnowRunner-Source-Code/blob/fded36a6126b199d2d3253968a3e9cc5ca1ef8e9/src/asi/dllmain.cpp) launches initialization on a new thread and invokes shutdown during process detach. These are application startup changes, not a documented SnowRunner plugin API.
- The [graphics implementation](https://github.com/ElAmine01/Minimap-For-SnowRunner-Source-Code/blob/fded36a6126b199d2d3253968a3e9cc5ca1ef8e9/src/asi/hooks/DXGIHook.cpp) discovers presentation/resize functions using a temporary swap chain, installs MinHook hooks and captures a device from presentation. The [UI manager](https://github.com/ElAmine01/Minimap-For-SnowRunner-Source-Code/blob/fded36a6126b199d2d3253968a3e9cc5ca1ef8e9/src/asi/render/ImGuiManager.cpp) releases its render-target view on resize and subclasses the game window for input. This entails graphics and input lifecycle responsibilities beyond simply loading a DLL.
- The [offset scanner](https://github.com/ElAmine01/Minimap-For-SnowRunner-Source-Code/blob/fded36a6126b199d2d3253968a3e9cc5ca1ef8e9/src/asi/game/OffsetScanner.cpp) detaches its worker; shutdown sets a stop flag under a mutex and notifies it. A stop request does not prove worker completion before DLL unload. This source review does not establish a safe hot-unload protocol.

Engineering assessment: the startup path cannot be adopted as our safe default. Microsoft warns against library loading and thread synchronization from `DllMain`, which executes under the loader lock. A worker started from there does not itself solve subsequent shutdown ownership. This is a review of code paths, not an observed crash or a claim about every possible execution of the community mod. [Microsoft DLL guidance](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices)

## Licensing and a distinct loader alternative

The separate [binary distribution at 3d21af8](https://github.com/ElAmine01/Minimap-For-SnowRunner/blob/3d21af8225e8ce2a616afea14652b1c832a6347d/LICENSE) contains an MIT license. The complete, non-truncated source tree at fded36a contained no LICENSE/COPYING file and its README supplied no source grant; GitHub's current source-repository license metadata was null. We have not established that the separate distribution grant covers this source tree, its contributors, dependencies or remotely obtained imagery. No source reuse was needed for this change.

[Ultimate ASI Loader 56fa0b0](https://github.com/ThirteenAG/Ultimate-ASI-Loader/tree/56fa0b03920f2a71174126e03a05c77d48b74744) is a different project with its own [MIT license](https://github.com/ThirteenAG/Ultimate-ASI-Loader/blob/56fa0b03920f2a71174126e03a05c77d48b74744/license). Its [README](https://github.com/ThirteenAG/Ultimate-ASI-Loader/blob/56fa0b03920f2a71174126e03a05c77d48b74744/readme.md) documents deferred initialization by default and an explicit `InitializeASI` export after loading. The [plugin implementation](https://github.com/ThirteenAG/Ultimate-ASI-Loader/blob/56fa0b03920f2a71174126e03a05c77d48b74744/source/loader/plugins/plugins.cpp) corroborates invoking that export. This is a useful design distinction from the minimap's custom proxy.

It is not a drop-in compatibility solution: the [startup implementation](https://github.com/ThirteenAG/Ultimate-ASI-Loader/blob/56fa0b03920f2a71174126e03a05c77d48b74744/source/loader/startup/startup.cpp) modifies import call paths and includes a wrapper preventing unload of the loader itself. Its module-lifetime assumption and multiple plugin/dependency paths need separate review. A source license and deferred entry point do not establish safe coexistence with SnowRunner or another hook owner. No alternate entry path, protection-related machinery or third-party loader was adopted, configured or tested.

## Original improvement delivered

Keep our diagnostic DLL's explicit create/update/destroy API outside automatic startup. Improve the separately owned renderer's resource lifecycle instead of reproducing the community startup code:

- `resize(64..2048)` builds a complete replacement texture/view/readback set before committing it; invalid dimensions preserve the active renderer. Its private context releases its old render-target binding before the old set is released.
- `resize(0)` or `suspend()` releases the owned device and all its resources. Rendering then returns size zero and no pixels. A later nonzero resize creates a fresh WARP device; failure to create it leaves the renderer suspended.
- Old viewport geometry is rejected after resize. Recreated resources receive a cleared frame before any drawing. The caller must serialize all operations; this is not a concurrent renderer.

These are original changes to our offscreen implementation. Tests use real owned Direct3D textures, rendered pixel readback and device destruction/recreation with synthetic telemetry. They do not call a game swap chain, install a hook, simulate hardware removal or validate a game's shared context. Clearing the private context on destruction is deliberately limited to a context we own. [Microsoft ClearState documentation](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-clearstate)

Debug and Release each pass **7/7 CTest cases**. The renderer host now passes **56 assertions**, up from 22, covering several sizes, old-frame rejection, invalid-resize preservation, suspension, repeated suspension and recreation pixel equality. Allocation failure on a real device and actual GPU removal were not forced. Python code is unchanged; its prior tests were not rerun.

## Next boundary

The renderer lifecycle is now available for a future adapter, but the [native loading decision](loading-decision.md) still applies. The reviewed community route supplies implementation precedent, not the missing compatibility evidence. No game injection, installation edit, overlay/security change or production guard change occurred. Nothing is waiting for a general user permission or waiver. A native deployment proposal still needs a reviewed bootstrap and ownership/shutdown design for the actual integration; more repetitions of the owned fixtures cannot establish it.
