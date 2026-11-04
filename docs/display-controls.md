# Display controls and navigation follow-up

The current diagnostic configuration selects one map texture, identity and affine
calibration. It does not automatically discover or switch maps. Geometry must
remain `alignment_preview` until measured alignment is established.

The game-specific publisher now converts its inspected lateral X/Z basis to the
forward X/Z basis before writing a packet. Generic wire encoding is unchanged.
Restart the publisher when updating it; an existing Python process retains its
previous code. Replacing the native renderer requires normal game exit/restart.

M and Escape no longer permanently disable the display. F9 is the explicit
visibility toggle. This fixes the latch, not automatic menu detection: the layer
can remain visible over the full map or pause screen while matching telemetry is
available. A verified game-state signal is still required for proper suppression.

Optional session settings (defaults shown):

```
zoom=1
tracking=motion
shape=circle
viewport=50 50 360
```

Tracking accepts `north`, `heading`, or `motion`; shape accepts `circle` or `square`.
Zoom ranges from 0.25 to 8. Viewport is left/top/size in pixels, size 96–800, and
must fit the current backbuffer. Out-of-bounds viewports fail closed. The controller
reloads display fields on its background thread; asset, session identity and
calibration remain fixed until restart. Present performs no file reads.

`python tools/minimap_settings.py` opens a local Windows settings companion using
standard Tk controls. Apply writes preferences atomically; return to the game to
see them. This is not an entry in SnowRunner's own settings menu. No input hooks or
game keyboard bindings are installed. The companion neither installs DLLs nor
starts telemetry. Motion-up uses position changes in map coordinates and holds
the last travel bearing when stopped; heading-up follows the corrected truck
basis, including while reversing. Initial motion-up falls back to truck heading.

## Remaining navigation work

1. Establish a bounded, build-specific, read-only source for active objectives,
   selected waypoints, and their map identity. Distinguish destination types and
   preserve user selection. Do not infer coordinates from task names.
2. Project destinations with the same affine transform and map rotation as terrain.
   Interior points use icons. Outside points intersect the circle radius or square
   boundary with padding for edge arrows; direction uses the unclamped vector.
   Reject wrong-map/nonfinite/stale points, and cap counts before rendering.
3. Verify interior, behind-player, corner, near-edge and off-map destinations using
   owned fixtures, then compare real markers against the full game map. No live
   destination source or destination rendering is implemented yet.
4. Add a per-map catalog of privately imported textures and measured calibrations.
   Switch atomically on verified map identity; hide during unknown maps/transitions.
   Never reuse the previous map texture for a new map.
5. Integrate a native in-game settings panel only after input focus/cursor ownership
   and menu lifecycle are established; the companion provides immediate controls.

The fixes need a new controlled live drive and map open/close comparison. Owned
pixel tests establish rotation and square clipping, not real-world calibration or
game transition correctness. Original assets/tests only belong in this repository.
