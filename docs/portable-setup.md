# Windows setup and startup

The portable folder contains SnowRunnerMinimap.exe, its `_internal` runtime, and
the original DLL `package` directory. Extract the whole folder together. Python
is bundled; users do not need Python, a compiler, process IDs or layout files.
This is an experimental unsigned build for one fingerprinted Windows Steam
version, not a guarantee for every store, game update, custom map or PC.

1. Close SnowRunner. Open SnowRunnerMinimap.exe and select the game root (the
   folder containing Sources and preload). Installed Steam libraries are offered
   as choices. No administrator permission or security-setting changes are used.
2. Choose **Install / repair** and review the target. Unknown existing dxgi.dll
   or d3d11.dll files are refused rather than overwritten. For the exact original
   cbf9772 development pair, **Migrate known development build** explicitly
   backs up and records ownership first; then choose Install / repair. Other
   manual installs need their documented rollback or separate review. A matching
   filename alone never establishes ownership.
3. Choose **Import maps**. Textures and geometry are read from your installed
   archives into your local application-data folder. They are never downloaded,
   uploaded or included in the distributed package. Coverage errors identify
   unsupported or ambiguous entries; re-import after installing additional DLC.
4. Optionally choose your exact CompleteSave[slot].cfg for saved destinations.
   Leave it blank to omit destinations. No newest-file or save-slot guessing.
   Diagnostic recording is optional and off by default.
5. Choose **Start game + minimap**, then Continue in SnowRunner. The launcher
   matches the exact executable path and fingerprint, creates a fresh session,
   and starts the map/destination helpers. It does not scan for unknown layouts.
   Only one startup controller is admitted. A different installation or game
   update produces an error; do not bypass that check.

Keep the session-control window open while playing. Closing it requests a clean
stop. The feed ends when its specific game exits, a stop is requested or eight
hours elapse. Close the old controller before starting a new game session. The
minimap hides stale packets. Local startup logs are in
`%LOCALAPPDATA%/SnowRunnerMinimap`. Setup is a separate installer application;
it is not the requested integration into SnowRunner's built-in Settings menu.

Close the game and use **Uninstall** to remove only unchanged DLLs owned by this
installer. Saves, imported maps and preferences are retained. **Recover** handles
an interrupted installation using its verified journal/backups, refusing any
files changed by another tool. Details: [installer](installer.md).

## Current limits

The in-game F10 mod panel remains a temporary interface. Game Settings row
registration and menu-state binding are unresolved; see
[Settings investigation](game-settings-integration.md). Destinations reflect
completed saves, not live updates. Gateway behavior and full every-map alignment
need live verification. Imported geometry remains an alignment preview.

## Build locally

Use an already installed PyInstaller and a locally tested Release DLL pair:

```
cmake -S experimental/dxgi -B build-portable -G "Visual Studio 17 2022" -A x64 -DSRMM_BUILD_FACTORY_ROUTE=ON -DSRMM_BUILD_GAME_PROBE=ON -DSRMM_STATIC_RUNTIME=ON
cmake --build build-portable --config Release
ctest --test-dir build-portable -C Release --output-on-failure
python tools/build_portable.py --native-build <Release-folder> --output <new-folder> --version <version>
```

The experimental native build links the MSVC runtime statically by default,
avoiding a dependency on a separately installed MSVCP140 runtime. Windows graphics
components and drivers are still required. A runtime-linkage change is tested in
owned hosts; it is not a replacement for clean-machine and live-game acceptance.

The builder performs no downloads. It emits a folder bundle, build log and SHA256
inventory. `SnowRunnerMinimap.exe --self-check` imports its workers without game
access or a UI. Bundled workers dispatch through the same executable, because a
frozen executable is not a Python interpreter. This follows
[PyInstaller runtime documentation](https://pyinstaller.org/en/stable/runtime-information.html).
Checksums detect changes; they do not replace publisher signing or establish
clean-machine compatibility. Do not disable Windows protections to run it.
