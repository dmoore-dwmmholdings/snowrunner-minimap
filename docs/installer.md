# Installer ownership and recovery

`tools/install_manager.py` installs only the original `dxgi.dll` and `d3d11.dll`
pair into an explicitly selected game's `Sources/Bin`. It never edits saves,
Steam settings, security settings, map assets, or the native game executable.
It does not elevate, download dependencies, register a service, or launch a game.
The setup/launcher application supplies the user-facing flow separately.

The initial supported executable fingerprint is the recorded x64 Steam build.
An unknown executable is refused for installation, even if a package advertises
another fingerprint. A later game update does not prevent uninstall or recovery
of an unchanged owned DLL pair. Supporting another storefront/version needs
separate compatibility work, not an override switch.

## Package preparation

Create a new local package from a trusted original Release build:

```powershell
python tools/package_minimap.py --build-dir build-dxgi-vs/factory-route/Release --output-dir build/package --version local-build
```

The package contains only two DLLs and `package.json`. File hashes, size limits,
PE32+ x64 architecture and DLL flags are validated. Extra build files and game
assets are never copied. SHA-256 detects changes; it does not authenticate a
publisher. A packaged setup executable may accompany these files but is outside
the DLL installer's file ownership. No downloaded binary is executed by this tool.

## Plan, apply and uninstall

All CLI operations are read-only plans unless `--apply` is present. Use a
dedicated private state directory outside the game installation and keep it for
future uninstall/recovery. Example paths below are placeholders:

```powershell
python tools/install_manager.py discover --steam-dir "C:\Program Files (x86)\Steam"
python tools/install_manager.py install --game-dir "D:\SteamLibrary\steamapps\common\SnowRunner" --package build/package --state-dir "C:\Private\MinimapInstaller"
python tools/install_manager.py install --game-dir "D:\SteamLibrary\steamapps\common\SnowRunner" --package build/package --state-dir "C:\Private\MinimapInstaller" --apply
python tools/install_manager.py uninstall --game-dir "D:\SteamLibrary\steamapps\common\SnowRunner" --state-dir "C:\Private\MinimapInstaller" --apply
```

Steam discovery reads library and app manifests and returns choices. It does
not choose between several installations or select a save slot. An explicit
game directory is also accepted. Symlinks and junctions are refused.

For a first install, either existing proxy DLL causes a refusal. The installer
does not infer ownership from a filename or silently adopt files installed
manually. An explicit setup migration recognizes only the hardcoded exact
original cbf9772 pair and supported game fingerprint. It requires a closed game,
no existing receipt or pending transaction, a process lock, and verified private
backups before recording ownership. Adoption does not change either DLL; normal
upgrade follows separately. Unknown development builds need separately reviewed
removal using their original backup records. Do not delete an unknown DLL
to get past this check. Upgrades require the same game directory, an ownership
receipt, and both unchanged installed hashes.

Before modifying the pair, the installer records a transaction journal and
verified private backups. Per-file replacement uses staged files and atomic
rename; the pair itself is not an atomic filesystem transaction. The game must
remain closed throughout. Running-game checks repeat during mutation. Ordinary
errors trigger rollback; process interruption leaves a journal for recovery:

```powershell
python tools/install_manager.py recover --game-dir "D:\SteamLibrary\steamapps\common\SnowRunner" --state-dir "C:\Private\MinimapInstaller" --apply
```

Recovery accepts only the recorded before/after states, verifies backups and
the ownership receipt, and refuses changed, foreign or incomplete staged files.
Keep the game closed while recovery is pending. A concurrent game start or
external file modification can leave a partially changed pair requiring review;
the tool does not force-kill the game, change permissions, or erase the conflict.
Backups and completed/recovered journals remain for inspection. Uninstall removes
only the two unchanged owned DLLs; settings, private map cache, saves and the
portable setup application are preserved.

## Validation and limits

Windows tests exercise actual temporary files and locking, injected
write failures, process-interruption recovery, unchanged-data preservation,
foreign-file conflicts, package integrity/architecture, Steam library metadata,
and game-update cleanup. Game-presence checks are injected in these fixtures;
they do not start SnowRunner or establish live compatibility. The existing
development installation was not modified by these tests. Complete setup and
clean-machine installation still require acceptance testing of the assembled
distribution. Protected game folders may produce access-denied errors; no
automatic elevation or permission changes are attempted.
