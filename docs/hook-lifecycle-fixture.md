# Owned hook-chain lifecycle design

`srmm_hook_lifecycle_host.exe` tests a hook-preserving lifecycle in its own process. Two original C++ callback layers stand in for a known hook chain. Both layers remain in the path to normal Windows `LoadLibraryExW` and `FreeLibrary` calls. Their counters are checked independently. No system entry point, machine code, Steam component, security software or other process is modified.

This is an explicitly synthetic compatibility model. It does not reproduce binary detours, validate Steam overlay compatibility or change the game's loading guard. The executable accepts no arguments, PID or DLL path. It resolves only its own adjacent, locally built `srmm_diagnostic.dll` and a fixed nonexistent filename used for a failure case.

## Design exercised

The client acquires a configuration-generation lease before loading. A stale generation or unknown test configuration is rejected before the loader is called. The cooperative fixture freezes configuration changes for the entire lifetime of the owned module reference. The client can call only the outer callback; it cannot select the inner callback or a downstream implementation to skip a hook.

Successful loading negotiates the original diagnostic ABI and creates a synthetic session. Shutdown stops further work, destroys that session, clears API pointers, then releases the owned module reference through both hook layers. The fixture observes module absence after cleanup. Windows tracks per-process DLL load references, and a handle obtained only for observation does not acquire one; the fixture never passes its observational `GetModuleHandleW` result to `FreeLibrary`. [Microsoft FreeLibrary documentation](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-freelibrary)

The load uses an absolute adjacent path with DLL-directory and System32 dependency search flags. This is a test-host loading contract, not authentication of arbitrary plugins or a promise about remote loader behavior. [Microsoft LoadLibraryExW documentation](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryexw)

| Condition | Tested behavior |
| --- | --- |
| Duplicate load | Refused without another Windows load reference |
| Unknown or stale chain configuration | Refused before calling the loader |
| Configuration change during a lease | Refused by the cooperative fixture |
| Missing DLL | Load failure releases the configuration lease |
| ABI mismatch | Loaded module is released through both hook layers |
| Controlled unload failure | Session and API pointers are cleared; module reference and lease stay tracked in cleanup-pending state |
| Caller deadline expires during delayed load | Result remains pending; no retry or premature unload; worker is joined before cleanup is reconciled |
| Stop requested during a borrowed API operation | DLL/session stay alive; concurrent work is refused; cancelled result is suppressed; unload follows operation completion |

A pending result is not proof of failure. Cancellation uses an epoch so requests are not erased by resetting a boolean. The owner always joins the operation and calls close again to reconcile a stop racing the final ready-state transition. It never frees request/session storage while an operation may still use it. An unload failure is not retried automatically; the explicit retry test is limited to a deliberately injected failure known to occur **before** `FreeLibrary`.

The fixture's controlled wait gates have a five-second bound. RAII releases a gate if an assertion throws, and the async operation's owner joins before referenced objects are destroyed. CTest has a 20-second timeout for this owned test process only. There is no game-process termination path.

## Verification

```powershell
cmake --build build --config Release --target srmm_hook_lifecycle_host
.\build\Release\srmm_hook_lifecycle_host.exe
ctest --test-dir build -C Release --output-on-failure
```

The host passes **66 assertions**, including ten repeated complete load/API/unload cycles; these are not 66 unique scenarios. Both x64 Debug and Release pass **7/7 CTest cases**. The new lifecycle case also passes ten successive Release executions to check its controlled concurrency paths. A manual argument-rejection check exits 2 before loading. No Python source changed; the earlier 32 passing Python tests were not rerun.

## Exact remaining boundary

The production game launcher and its byte-comparison guard are unchanged. This fixture can freeze a chain it owns; it cannot freeze or authenticate a live third-party hook chain. It does not cover remote-thread calling conventions, loader-lock interactions, third-party hook mutation, private binary trampolines, DLL initialization hangs, graphics callbacks, or actual game transitions. The delayed-load test pauses an original callback before the Windows loader; it does not deliberately stall Windows or another DLL's initialization.

Before another game attempt, a separately reviewed design must establish the actual canonical entry point, hook ownership, permitted dependencies and lifetime, and what happens if that evidence changes while work is pending. A filename, disk signature, passing synthetic fixture or removal of the byte guard is insufficient. Keep Steam's hooks in place and preserve refusal of unknown or changed state. Native initialization/shutdown and rendering integration remain separate, unverified work.
