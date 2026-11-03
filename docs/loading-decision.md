# Native loading decision

Reviewed 2026-11-03. This report uses public documentation and original repository code only.

**Decision: native game loading remains blocked by unestablished compatibility.** No documented route reviewed here supplies the missing ownership and lifetime contract for an existing third-party hook chain. Preserve the production guard and stop further injection attempts. More repetitions of our cooperative fixture cannot establish that external contract. This is a bounded engineering conclusion, not a claim that every possible SnowRunner extension route has been disproved.

## Alternatives reviewed

| Alternative | Documented guarantee | Assessment for this project |
| --- | --- | --- |
| Module reference with `GetModuleHandleExW` | Acquires a reference to a module already loaded in the calling process; `FROM_ADDRESS` identifies its containing module. `PIN` retains it until process termination. [Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandleexw) | A reference preserves the module mapping, not an independent private trampoline, hook configuration or vendor shutdown state. Pinning prevents reversible unload and still requires execution inside the target. Neither establishes a bootstrap contract. |
| DLL load/unload notifications | Notifications describe module events. The callback must not call functions in other modules and runs before dynamic linking. [Microsoft](https://learn.microsoft.com/en-us/windows/win32/devnotes/ldrdllnotification) | Observation is not a lease preventing hook mutation. Calling a loader/coordinator from this callback would violate its restrictions. Queuing work elsewhere still leaves a race between observation and use. |
| Detours transactions | Attach/detach rewrite a target and manage a trampoline; enlisted threads have instruction pointers adjusted on commit. [Microsoft Detours](https://github.com/microsoft/Detours/wiki/Using-Detours) | These mechanisms manage the hooks participating in that transaction. The reviewed API establishes no ownership or lifetime agreement with an unrelated existing hook implementation. Adding another hook would change the target without resolving that missing agreement. No Detours dependency was added. |
| Steamworks overlay interface | Documents automatic overlay hooking, UI activation, status and notifications. [Valve](https://partner.steamgames.com/doc/features/overlay) | The reviewed interface exposes no acquisition/release contract for its internal loader hooks or a custom minimap rendering callback. UI activation does not establish either capability. |
| Publisher mod workflow | Sample truck mods use XML, meshes and related content; the SnowRunner gameplay-entity introduction documents custom cargo. [Sample workflow](https://expeditions-guides.saber.games/truck_modding/getting_started/sample_mod_by_the_game/overview/), [Gameplay entities](https://expeditions-guides.saber.games/custom_gameplay_entities/custom_gameplay_entities_intro/) | These reviewed pages do not establish a native DLL entry point, arbitrary script extension loader, HUD callback or shutdown protocol. This finding is limited to the reviewed documentation. |

The assessment column contains engineering inferences from the stated API guarantees. Keeping a module mapped and keeping an entire hook chain valid are distinct requirements. Module-name or signature allowlisting, repeated byte snapshots and following a jump destination do not provide synchronization with the owner of that chain.

## Risk review against the implementation

The [owned fixture](hook-lifecycle-fixture.md) gives every configuration writer the same mutex and lease protocol. That cooperation is why it can prevent mutation while an operation is pending. No equivalent agreement has been established for a foreign chain. Transplanting the fixture's lease object into a launcher would not make other participants honor it.

The production launcher checks a bounded export-byte snapshot before a call. Preserve this rejection boundary: a mismatch remains a refusal. Even a matching snapshot is a diagnostic check, not proof of all subsequent runtime behavior or freedom from races. The fixture therefore does not justify relaxing the guard or authorizing a new game attempt.

Initialization also has a separate constraint: `DllMain` runs under the loader lock, where loading libraries or synchronizing with other threads can deadlock. The diagnostic DLL's explicit API is useful preparation, but does not supply a game-owned initialization or shutdown callback. [Microsoft DLL best practices](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices)

Switching to a lower-level loader entry, replacing a proxy DLL, changing startup order or disabling an overlay would change the loading path without resolving this review's missing contract. None was implemented or tried. No protection bypass, hook removal, guard relaxation or game modification is part of this decision.

## Evidence required to resume

Resume native integration when concrete evidence supplies a supported game/host extension lifecycle, or documented cooperation with the relevant hook owner: canonical entry and calling context, dependency and callback lifetime, mutation/shutdown coordination, and behavior for a pending or failed operation. Review that evidence against an exact implementation before proposing a bounded runtime test. A general approval to continue, a recognizable DLL name or another synthetic pass does not supply the missing technical evidence.

After loading is resolved, real position/heading validation, map alignment, rendering callbacks and transition cleanup still require separate game tests. There is no working in-game minimap yet. No external overlay has been substituted.

This pass changes documentation only. The prior owned-fixture checkpoint remains 7/7 CTest cases in both Debug and Release, including 66 lifecycle assertions; tests were not rerun for this documentation change. No new game access or runtime loading operation occurred. Native integration stays incomplete; this report ends the current loading investigation until materially new evidence is available.
