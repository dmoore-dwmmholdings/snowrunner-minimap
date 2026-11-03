# Windows inspection checkpoint

Observed 2026-11-03, starting from commit `f1d347ef12c4fc26e05e3c56c478246c54405a0a`.

## Build and tests

The repository was cloned into the requested Windows checkout. Visual Studio 2022 Build Tools, MSVC 19.44.35209.0 and Windows SDK 10.0.26100.0 built the x64 Debug prototype. All 3 CTest cases pass: 27 geometry checks, 48 adapter checks and 11 new PE inspection checks. The PE tests also pass under Windows PowerShell 5.1. The console demo generates `build/synthetic-preview.svg`; it still explicitly uses synthetic data with no game connection.

The identity collector now checks PE architecture rather than leaving it unverified, records product version and optional Steam build ID separately, and defaults unobserved runtime fields to `Unknown`. Malformed/truncated headers, mismatched PE machine/header pairs, DLLs and unsupported machines are rejected. These are inspection checks, not executable trust or game compatibility checks.

## Installed artifact observations

| Field | Observation | Evidence |
| --- | --- | --- |
| Storefront | Steam, app 1465360 | Installed Steam manifest |
| Steam build ID | `26218478` | Installed Steam manifest |
| File version | `1.0.0.0` | Windows version resource |
| Product version | `1.907887.SNOW_DLC_19` | Windows version resource, not UI version |
| Architecture | x64 / PE32+ | PE machine `0x8664` and optional header; independently corroborated by MSVC dumpbin and REA PE resource inspection |
| Executable size | 46,810,152 bytes | Read-only file stream |
| SHA-256 | `d46623b23808d19a40f7ebeec15332d11f97317637ec4d4e072a501bc78a77c8` | Read-only file stream |
| Graphics imports | `d3d11.dll` / `D3D11CreateDevice`, `dxgi.dll` / `CreateDXGIFactory1` | MSVC dumpbin import table |
| Live process | Initially stopped; normal Steam launch subsequently reached the main menu | Windows process enumeration and game UI |
| Display mode | Borderless, 3440 x 1440, target display 1 | Game Video settings, read without changing them |
| Loaded graphics modules | D3D11, DXGI, D3D12 and NVIDIA `nvwgf2umx` | Read-only process module enumeration |

DX11 is a candidate based on imports, but both D3D11 and D3D12 were loaded. The active renderer, displayed UI version, loaded mods, live map and truck state are unverified. The executable hash identifies this artifact only; it does not allowlist it for telemetry or rendering. No debugger was attached, no game memory was scanned, and no DLL was injected. UI inspection stopped when the automation tool detected user input; the game was left running.

## REA evidence

The installed REA 6.5.0 package and Ghidra 12.1.4 configuration were found. REA MCP tools were not exposed in the active agent session, so the installed CLI was used. No software was downloaded or security configuration changed. The sandboxed provider self-test hit a filesystem permission error; authorized local execution allowed static inspection to proceed.

REA `inspect_pe_resources` returned evidence ID `ev_f264fe62bd6cf3f7e6c6165bca1bf72e3ce565981b396ae7d104f38d2180d47c` for this executable, identifying PE32+ and machine 34404 (`0x8664`). Its resource inventory is static; opaque payloads and icon relationships do not establish runtime behavior.

The subsequent whole-program `analyze --provider ghidra` request failed with `provider_timeout` during `list_documents`: the Ghidra startup/analysis deadline elapsed after 330,000 ms. The provider admitted the exact NTFS artifact and began analysis of its private snapshot, but no completed analysis or usable telemetry references were returned. REA cleaned up its worker and temporary directory; the diagnostic report remains local. This was a tool timeout, not an approval denial. Do not rerun the same broad request as if it had succeeded; the next static pass needs a narrower analysis profile or a deliberately adjusted analysis budget.

Raw evidence stays local in ignored `build/` files: `windows-identity.local.json`, `runtime-modules.local.json`, `rea-pe-resources.local.json`, `pe-headers-imports.local.txt`, and `rea-analysis.local.json` (the Ghidra timeout diagnostic, not completed analysis). No executable bytes, recovered game code, maps, textures, saves or account information belong in the public repository.

## Next integration step

Continue with a scoped static analysis pass to locate candidate map/vehicle data references on this fingerprint. Validate any candidates through read-only inspection in a single-player map against controlled movement, and confirm the active renderer independently. No live source or offsets have been established yet. The user's read-only REA inspection authorization covers investigation; there is no concrete DLL/hook installation ready for approval yet.

Only after a real data source is validated should the renderer consume native samples. DLL loading, injection, hooks and game-directory modifications require a separate concrete proposal and approval. No such operation was performed. Map imagery and coordinate calibration also remain unresolved. SNOW-4 remains unfinished; a successful Windows build does not complete the in-game minimap.
