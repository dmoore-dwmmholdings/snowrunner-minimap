# Native in-game display panel

The original D3D11 panel renders in the game's swap chain. It is not an entry in
SnowRunner's built-in Options menu and does not use the separate Tk companion.
The panel uses locally installed Windows Segoe UI glyphs, rendered with GDI into
an owned bitmap. No fonts, game assets, or third-party UI library are bundled.

Controls:

- F10 opens or closes the panel.
- F1/F2 select a row.
- F3/F11 decrease/increase the selected value.
- Select the Restore defaults row, then F3 or F11 to reset display preferences.

Rows cover zoom, north/truck-heading/motion tracking, round/square shape, size,
four corners, horizontal/vertical pixel offsets, and restoring defaults. Values stay within the
current backbuffer. A held key fires once until released. Background windows
cannot change settings. Explicitly opening the panel with F10 enables editing in
the foreground. Menu detection is advisory and does not gate reversible display
edits. The panel advises pausing before editing when no menu signal is available.
The panel does not intercept or consume game input, install a window hook, or
alter Steam's overlay.

F5 was removed after checking Saber’s official documentation: it opens the
saber.games menu on the title screen. F4 is also avoided. The local user settings
file did not expose a keyboard remap table, so this does not establish that every
remaining function key is unused in every game screen or custom configuration.
Because the game also receives these keys, pause before editing and check for
simultaneous game actions in the combined live test. This is separate from the
still-independent work on automatically hiding the minimap in game menus.

The footer explicitly distinguishes saved destinations from live destinations and
reports the last-save age supplied by the caller. Alignment previews remain
labelled as unverified.

## Integration

Add `native_settings.cpp` to the game-session targets. Link `gdi32` and `user32`
alongside `d3dcompiler`; the owned fixture also links `d3d11` and `dxgi`.

Construct `SrmmNativeSettings` on the Present owner thread. Pass the current
display values and backbuffer dimensions to `update`, with input from
`srmm_poll_settings_input(output_window, optional_menu_visible_hint)`. Apply any returned
settings immediately and persist them on the background session thread. Do not
write the configuration from the rendering callback. Supply the current
calibration/waypoint-source status through `navigation_status`.

Call `draw` when the panel is open and the game is foreground, including menus
where the minimap itself is suppressed. Skip DXGI test presents. The class checks
its thread owner and device identity. Its independent deferred context executes
with `RestoreContextState=TRUE`, and it releases command lists, render-target
views, and backbuffer references at the end of every draw. Device changes rebuild
the owned graphics resources. Text textures update only when displayed content
changes; no filesystem access occurs in update/draw.

## Validation

`native_settings_host.cpp` runs deterministic model checks and an owned hidden
WARP swap-chain fixture. It verifies all settings, key edges, focus/menu gating,
viewport clamping, actual panel/text pixels, application viewport and primitive
topology restoration, and successful swap-chain resize after drawing.

The revised Release run passed 36 checks. This is isolated graphics validation;
the combined SnowRunner build still needs one coordinated in-game test.

References: [ExecuteCommandList](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-executecommandlist)
and [CreateDIBSection](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createdibsection).
Key-binding evidence: [Saber’s account-linking instructions](https://support.saber.games/hc/en/snowrunner/articles/how-to-link-snow-runner-to-saber-games-account-23).

The integrated controller reports pending, saved and failed persistence in the panel. Failed writes retain the visible edit while retrying; they are never silently reported as saved.
