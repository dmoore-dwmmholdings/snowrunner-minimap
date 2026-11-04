# Rotation, visibility and map catalog checkpoint

Rotation previously stepped directly between sampled bearings, used a large UV
displacement threshold, and traversed multiple 100 ms polling stages. The new
render-thread bearing filter follows the shortest angular arc with a 60 ms time
constant, independent of frame rate. The threshold is reduced; stopped motion
keeps its last bearing. The publisher samples at 20 Hz and game ingestion/handoff
poll at 20 ms. Display configuration reloads remain on the background thread.
No stale data is extrapolated into a visible frame; the existing freshness gate
still applies before smoothing. Long gaps reset interpolation.

Owned tests cover turn progression, wraparound, frame-rate independence and stale
reset. Live smoothness still requires deploying the new DLL after normal exit and
starting a new publisher. An existing process does not acquire source changes.

Settings-page suppression is **not fixed**. Pose availability does not indicate
gameplay visibility, and detecting Escape/M key presses cannot reliably track
nested menus or mouse/controller navigation. F9 remains an explicit workaround.
Automatic suppression requires a validated runtime menu/HUD-state source with
foreground, map, gameplay and lifecycle identity checks. Do not infer menu state
from stationary position or unchanged physics: parked gameplay is also stationary.

The original destination projection helper now computes padded circular/square
edge intersections and bearings using the map's rotation. Tests cover corners,
coincident positions, rotation and invalid input. It is not connected to a live
destination source or shader markers yet.

`tools/map_catalog.py` discovers canonical map texture names across explicit local
archives, reports duplicate sources as ambiguous, and performs no extraction or
game writes. Its selection primitive clears the old asset immediately on identity
change and rejects late generations and uncalibrated replacements. Tests use
original synthetic archives. These primitives are not connected to native loading:
the live renderer still supports its one configured map. Import, calibration,
background preparation and atomic native admission remain required for multi-map
support. Private catalog outputs and extracted textures must stay local.
