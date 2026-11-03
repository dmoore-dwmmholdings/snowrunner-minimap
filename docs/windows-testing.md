# Windows build and testing handoff

**Status: not run on Windows.** The current prototype is a console geometry demo, not an installable SnowRunner mod or overlay. Do not copy it into the game directory.

## Build the portable prototype

Use an existing Git installation, CMake 3.20+ and Visual Studio 2022 C++ build tools with a Windows SDK. Install missing tooling through its official vendor only after the machine owner permits it. In a Developer PowerShell:

```powershell
git clone https://github.com/dmoore-dwmmholdings/snowrunner-minimap.git
cd snowrunner-minimap
cmake -S . -B build -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
# UTF-8 output avoids Windows PowerShell's default UTF-16 redirection.
$preview = & .\build\Debug\minimap_demo.exe
[System.IO.File]::WriteAllText((Join-Path (Get-Location) 'build\synthetic-preview.svg'), ($preview -join "`n"), [System.Text.UTF8Encoding]::new($false))
Start-Process .\build\synthetic-preview.svg
```

The expected console test output is a check count and CTest success. The preview contains four synthetic heading samples and must say “no game connection.” Record tool versions and commit SHA with results. Local builds only; the repository contains no Actions workflow.

## Metadata collection before native work

The following repository script is prepared but **not tested on Windows**. It reads the named executable's metadata/hash and writes an ignored local manifest in `build/`. It never launches or attaches to the game. Supply the actual storefront, version visible in the game and graphics/display mode; use `Unknown` where appropriate. It does not inspect or verify the binary architecture or actual graphics API. Do not change an execution policy or security settings to make it run.

```powershell
.\tools\Collect-GameIdentity.ps1 -GameExe 'C:\path\to\SnowRunner.exe' -Storefront Steam -DisplayedVersion 'copy actual version' -GraphicsApi Unknown -DisplayMode Unknown
```

Review the output locally; it excludes the absolute game path and user/account information. The manifest is a starting input to [native integration validation](native-integration.md), not a supported-build declaration. Installed terms/protection policy, intended test setting and explicit approval for a concrete native runtime test remain required. Merely collecting this file does not authorize loading a DLL or modifying the installation.

## Future game acceptance checklist (all pending)

- Record exact game version/storefront, map, graphics API/mode, monitor layout, DPI and selected integration approach.
- Verify map identity, position, cardinal headings and latency from real game data.
- Validate image alignment at three calibration landmarks and additional independent checkpoints; record residual error and units.
- Verify north-up/heading-up, zoom, hide/show, focus, input pass-through and controller behavior.
- Verify garages, map transitions, vehicle switching, loading, paused game, process exit, stale data and reconnect behavior.
- Verify borderless/fullscreen, DPI and monitor changes as applicable; do not promise exclusive fullscreen support without a runtime test.
- For any game-directory files, document backups and exact uninstall list before installation. No current build modifies that directory.
- No malware protection exclusions or anti-cheat compatibility claims based solely on third-party README text.
