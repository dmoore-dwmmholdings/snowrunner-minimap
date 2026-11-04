# Saved destination markers

The native minimap can render up to 16 saved destinations. Amber outlined
diamonds identify saved waypoints; filled amber diamonds identify supported saved
objective locations supplied by the source. Amber arrows point toward destinations outside
the circle or square. Projection uses the same rotation and horizontal/vertical
world scale as the map image. The truck remains white.

These points come from the last completed save, **not live objective or waypoint
memory**. A fresh local transport packet only proves the publisher is running;
it does not prove the saved route matches edits made since the save. The source
save timestamp is preserved for the in-game settings/status panel. The currently
observed saved-coordinate conversion remains an alignment preview until checked
against a waypoint placed at the current truck in the combined game test.

## Local contract

`SRMMDST1` is a fixed 672-byte, little-endian sidecar. The 160-byte header uses
`<8s4I3Q64s32sQ2I>`: magic, version 1, total size, availability, PID, sequence,
GetTickCount64 sample time, session, zero-padded map identifier, executable SHA256,
save modification Unix milliseconds, source kind 1 (saved), and count 0–16.
Each 32-byte entry is `<II2dQ>`: kind 1 (waypoint) or 2 (saved objective), one-based ordinal, world X, world Z,
zero reserved. Unused entries must be zero. Unavailable packets have zero count
and save timestamp. Available empty routes are valid.

The native gate rejects wrong game/session/map/build, malformed or nonfinite
coordinates, altered repeated sequences, older packets, clock reversal and
transport older than 1500 ms. The renderer clears all markers on unavailable
input; it never retains a previous map's destinations. Explicit map alignment
previews retain their preview status. No save data or game assets belong in the
public repository.

## Integration

The background controller reads the sidecar into its immutable frame. The render
thread owns one `SrmmDestinationGate` per map/session generation, matching the
pose gate. After pose admission, zoom and rotation are applied,
`srmm_place_saved_waypoints(frame, destinations, calibration)` projects admitted
points and clears unavailable ones. `SharedRenderer` draws those projected
markers without additional file I/O or waits in Present.

Validation includes native parser/admission tests, Python-to-native packet
compatibility, and actual hidden WARP backbuffer pixels for diamonds, arrows,
rotation and rectangular map scaling. These tests are distinct from live source
alignment and do not establish live objective support.
