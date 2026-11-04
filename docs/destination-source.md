# Saved destination source

`tools/destination_source.py` reads one explicitly selected local `CompleteSave[slot].cfg` file. It extracts `CompleteSave[slot].SslValue.waypoints[current_level]`, preserving route order, and emits at most 16 points to `destinations.local.bin`. With `--zone-catalog`, it also resolves supported saved tracked cargo-objective zone IDs to same-map static scene positions. The source never writes a save, opens a game process, downloads assets, or infers the active save slot from modification time.

This is a **saved-destination alignment preview**, not a live route, objective or active-location source. A removed or moved marker remains represented by the last completed save until SnowRunner saves again. The packet carries the save file's UTC modification timestamp separately from transport freshness. The UI must identify these markers as saved destinations and must not imply that a fresh publisher timestamp makes the saved data current.

Objective resolution is deliberately limited to a tracked, unfinished, single-stage cargo-delivery objective with understood static zone references. Completed deliveries, unsupported actions, multistage objectives, unresolved zones and foreign-map targets produce no guessed objective markers. A target on another map cannot use this map's coordinate frame; gateway routing remains unresolved. The optional zone catalog is generated locally by `tools/map_zones.py` and contains original-game static geometry, so it must remain private.

The x/z conversion multiplies the saved coordinates by 150. This format lead is described in the community project's [maintenance notes](https://github.com/ElAmine01/Minimap-For-SnowRunner-Source-Code/blob/main/MAINTENANCE.md#in-game-waypoints-unfinished-discovery-done); the implementation here is original. No community implementation or game code was copied. The format was confirmed against a local save, but the numeric conversion still needs an independent marker-at-truck check on this build. Y is not used. The CLI requires explicit `--allow-saved-preview` until that validation is completed.

Example arguments, using private local paths:

```
python tools/destination_source.py --save <explicit-slot.cfg> --feed <session-folder> --pid <game-pid> --session <session-id> --seconds 600 --allow-saved-preview --feed-status <session-folder>/status.local.json
```

The publisher uses the existing diagnostic feed manifest, a single-writer lock, and the fresh pose packet's PID/session/build/map identity. It rechecks the map after reading the save and emits an unavailable packet when data is unavailable. Normal stop writes an unavailable snapshot. Abrupt process exit is covered by the reader's 1.5-second transport timeout. The maximum requested run is eight hours. The launcher supplies a status file whose fresh PID/session/run identity binds this helper to one publisher run; a terminal, changed or stale run stops this helper. An optional uniquely named stop file can also terminate one invocation. It never silently resumes another game/session.

## Binary sidecar, version 1

The fixed packet is 672 bytes, little-endian, with all unused bytes zero. Its first 160 bytes use `<8s4I3Q64s32sQ2I>`:

| Offset | Field |
| --- | --- |
| 0 | Magic `SRMMDST1` |
| 8, 12 | Version 1, size 672 |
| 16, 20 | Available 0/1, game PID |
| 24, 32, 40 | Sequence, sampled `GetTickCount64`, session identity |
| 48 | NUL-padded canonical map, 64 bytes |
| 112 | Executable SHA-256, 32 bytes |
| 144 | Save modification time, UTC Unix milliseconds; zero if unavailable |
| 152, 156 | Source kind 1 = saved-data snapshot; point count 0..16 |

Each of 16 records occupies 32 bytes (`<II2dQ>`): kind (1 = saved waypoint, 2 = saved tracked cargo-objective zone), one-based ordinal, world X, world Z, reserved zero. Waypoints come first, followed by supported same-map objective zones, with a combined maximum of 16. Coordinates must be finite and within +/-20,000 world units. An unavailable packet has zero count/time and no points. A valid empty saved list has available=1, its real save timestamp and zero count; it actively clears previously displayed points.

The native reader must reject wrong identity, stale/future timestamps, backwards sequence, unsupported kinds, invalid coordinates and nonzero reserved/unused bytes. The existing marker projection can place saved markers at their map position or clamp them to the circle/square edge. A saved-source glyph/legend must remain distinct from any future live source.

## One combined live validation

Place a waypoint on the truck's visible full-map marker, then resume driving until the game's normal save completes. Confirm that the saved marker appears at that position, stays fixed as the truck moves, and moves to the minimap edge when outside the current zoom. Move/remove it, allow another normal save, and confirm refresh/removal. Confirm the save-slot selection is correct before interpreting an empty list as no route. This does not validate live objectives; their runtime binding remains unresolved.
