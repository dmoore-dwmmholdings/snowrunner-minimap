# Combined Windows build checkpoint

This checkpoint combines the new features into one locally built experimental
startup-bridge pair. It has not been installed or validated in SnowRunner. The
earlier smoothing build remains the last user-tested build. No paid CI is used.

## Implemented and tested locally

- Game-bound feed lifetime, explicit stop, heartbeat, same-session resume and
  private recording. The control window waits for its old helpers before restart
  and retains ownership after a partial launch failure.
- Automatic local map import with independently cross-checked terrain bounds,
  origins and rectangular-map scales. The installed asset inventory yielded 67
  supported map identities; assets and generated catalogs remain private.
- Native map-generation replacement with immediate old-generation revocation,
  unknown-map suppression, settings preservation and retry after missing assets.
  File reads and preference writes stay off the render thread.
- In-game F10 display panel: zoom, north/heading/motion tracking, round/square,
  size, corners, offsets and defaults. F1/F2 select rows; F3/F11 adjust. Settings
  show saving/saved/failure feedback and persist through a cooperating file lock.
  This is a native-rendered mod panel, not a SnowRunner Options-menu entry.
- Saved waypoint and supported saved cargo-objective markers, with off-map edge
  arrows. Outlined amber diamonds identify saved waypoints; filled amber diamonds
  identify saved objective targets. The panel shows the source save's age.
- Optional context-bound UI-state ingestion. A verified menu state hides the
  minimap, fresh gameplay restores it, and unknown/stale input fails closed when
  this optional source is configured. No verified SnowRunner profile is bundled.

Owned tests use original synthetic assets, temporary files, hidden WARP devices
and owned child processes. They cover actual pixels, context restoration,
backbuffer lifetime, cross-language packets, file races, controller generations,
asset recovery, persistent settings and helper ownership. These tests do not
establish combined live-game behavior.

## Local validation on 2026-11-04

Release build succeeded. All 16 experimental CTest suites passed, including 149
actual background-controller assertions, persistent-settings file/lock checks,
hidden WARP rendering and Python/native wire compatibility. All 110 Python unit
tests passed. The combined diff passes `git diff --check`.

```powershell
cmake -S experimental/dxgi -B build-dxgi-vs -A x64 -DSRMM_BUILD_FACTORY_ROUTE=ON -DSRMM_BUILD_GAME_PROBE=ON
cmake --build build-dxgi-vs --config Release
ctest --test-dir build-dxgi-vs -C Release --output-on-failure
python -m unittest discover -s tests -p 'test_*.py'
```

The built pair is under `build-dxgi-vs/factory-route/Release`. Building it does not
install it. No live game calls, game-file changes or computer-use session were
part of this final non-interactive validation.

## Still unresolved or awaiting live verification

Automatic menu detection remains an engineering gap. Metadata and typed GUI
roots did not establish an actual Settings/pause/full-map signal. The optional
publisher requires repeated labelled runtime observations before a profile can
be admitted. No key-history, stationary-truck or cursor heuristic is substituted.

Destination changes currently follow completed saves, not live mission/waypoint
events. Supported cargo targets require a known single-stage saved objective and
an exact same-map zone match. Cross-map gateway routing is not implemented;
foreign-map coordinates are never projected onto the current map. The saved
waypoint scale still needs an in-game landmark/waypoint check.

Imported geometry remains `alignment_preview`, despite consistent scene/terrain
metadata. Actual texture alignment, map transitions, rectangular-map rotation,
menu return, display persistence, helper GUI behavior and Steam coexistence need
the coordinated combined-build test. The existing experimental loading guards
and original ABI/lifetime limitations remain in effect.

## One coordinated session when computer access is available

1. Verify the current installation and running state. Exit normally, back up and
   verify saves, archive only the known diagnostic DLLs/configuration, and install
   the combined pair. Do not change Steam overlay or security settings.
2. Prepare a fresh private session using the imported map/zone catalogs, an
   explicit save slot and the game-bound launcher. Confirm recording/heartbeat.
3. With the truck parked, observe gameplay, pause, Settings, full map and repeated
   gameplay returns. Validate an actual UI-state source before enabling automatic
   suppression; source plumbing alone is not completion.
4. Test F10 controls and saved/failure feedback, then drive with gentle turns.
   Check two separated landmarks, a normal map transition and a rectangular map.
5. Place/remove a waypoint and let the game save normally. Compare saved markers
   and supported same-map objective locations, including off-map arrows and age.
6. Check normal pause/map/settings returns, resolution/resource transitions and
   clean helper stop/resume. Record which behaviors were actually verified.

Computer interaction, live sampling, installation and restart are currently
paused at the user's request. Resume only after explicit permission to use the
Windows machine again. Preserve the current game and saves in the meantime.

Related: [map import](map-import.md), [saved destination source](destination-source.md),
[native settings](native-display-panel.md), [feed lifecycle](feed-lifecycle.md),
[menu-state boundary](menu-state-status.md).
