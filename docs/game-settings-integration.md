# SnowRunner Settings-menu integration: evidence and remaining boundary

The F10 panel is drawn in SnowRunner's swap chain, but it is not integrated into
the game's Settings menu. It remains a temporary diagnostic interface. This
investigation identifies the actual Settings components without claiming a
working registration or loading mechanism.

## Static findings

Read-only inspection of the observed Steam executable's installed archives found:

- `initial.pak` contains compiled SSLB modules for `UiSettingsController`,
  `UiSettingsView`, `UiSettingsLayout`, `UiSettingLayoutScreenPreset`, individual
  slider/stepper controllers and `UserSettings`.
- The layout metadata declares `UiSettingsLayout.presets` and
  `UiSettingLayoutScreenPreset.elements`. A screen preset also has a name,
  platform list and `hasAcceptButton` member.
- The controller declares `CreateInitialData`, `CreateData`, `Show`, `Hide`,
  `SubscribeContent`, `UnsubscribeContent`, `TryApplySettings`, `ApplySettings`
  and `ResetDefault`. Its metadata includes `layout`, `settings`, `activeTabs`,
  `currentSelectedTab`, and `settingSavesDelayed`.
- View-data metadata includes an array of settings rows. Row data includes a
  view, name, description, line and skip flag. The view references the game's
  focus manager. These are evidence of the real game-controlled menu hierarchy.
- `UserSettings` has inferred methods `SetAndSaveUserSetting(string, variant,
  string)`, `SetSettings(string, variant, string)` and `SaveUserSettings(string)`.
  References identify the active-user configuration load/save lifecycle and
  delayed-save operations. They do not establish permission to add arbitrary
  keys or the meaning of each string argument.
- `gfx.pak` contains `[gfx]/gfxbundle.gfxbundle`. Its observed S3DB table has
  115 entries. The name/size table covers the payload exactly; `settings.gfx` is
  one 96,359-byte entry. Only the 2,752-byte table was parsed for this inventory;
  no UI payload was decoded, copied, changed or executed.
- The inspected archive inventories did not expose a standalone Settings XML
  registration file. The existing XML vehicle/map mod paths therefore do not,
  by themselves, establish a UI extension route.

All member names and method shapes above are inferred metadata. Slots are not
runtime byte offsets, methods are not exported entry points, and matching names
do not prove a callable ABI. The inspection did not call game functions, inspect
the live process, patch script bundles, replace UI assets or change the game.

## Concrete integration target

The intended result is a Minimap category or section owned by the existing
Settings controller, using its ordinary slider/stepper rows, mouse/controller
focus, Apply/Back and Reset Defaults behavior. It must be reachable through the
game's Settings entry without an F-key or companion window.

An original adapter would reuse the minimap's bounded display-preference model
and background persistence. Opening the game menu would create an edit snapshot;
Apply would commit validated display values; Back/cancel would obey the game's
actual setting semantics; Reset Defaults would change only minimap preferences.
Menu callbacks would communicate through copied values, not retain render-thread
objects. Closing/rebuilding a screen would revoke its callbacks before destroying
rows; reopen would create a new generation. Game profile changes and process
shutdown would clear ownership. Existing game settings must remain untouched.

This is a target design, not implemented game-menu integration. Game-owned rows
cannot be constructed safely until their creation, callback and lifetime
contracts are established. A screen-looking overlay, adding an unknown key to
the save file, or substituting an unrelated built-in setting does not meet it.

## Exact unresolved engineering work

1. Establish a legitimate way to load original UI/controller definitions into
   this game build, or an actual host interface that creates Settings rows.
   The current D3D startup bridge provides rendering, not a script-VM interface.
2. Resolve the actual loaded Settings-controller instance, type/variant ABI,
   allowed execution thread and row factory, plus subscription revocation and
   destruction behavior. Static declarations alone cannot provide those.
3. Determine whether the game supports namespaced third-party preference storage.
   Until established, keep the minimap's separate local preference file rather
   than guessing game-save keys.
4. Validate keyboard/controller focus, Apply/Back/defaults, menu re-entry,
   language changes, profile changes and shutdown in the running game after an
   original adapter exists. Do not claim the existing F10 test covers these.

Useful next static work is locating the serialized Settings preset instances and
the loader that creates their controller/view objects. Runtime validation may
then be necessary, but another drive or blanket permission does not replace the
missing loader/ABI evidence. There is no user decision needed to continue that
bounded investigation.

## Follow-up: serialized presets located

A bounded read-only archive inspection located typed Settings object
descriptions in `initial.pak`'s `initial.cache_block`. This is stronger evidence
than controller declarations alone: the cache contains Settings layout and
screen-preset instances, interactive elements referencing the built-in setting
controllers, a Settings controller description with its input list, and tab,
default/reset and accept/back descriptions. Platform-specific preset names and
standard game-setting names occur together in these descriptions. No game
content was copied into this repository.

The original read-only `inspect_cache_index.py` now validates the observed
resource-container framing: 2,000 length-prefixed names, 64-bit offsets, 32-bit
sizes and an opaque zero-valued field. Every resource range is contiguous and
together they cover the payload exactly. Unknown headers, markers, flags and
noncontiguous ranges are rejected. Five synthetic tests pass, including every
truncation of the fixture, overlapping/gapped ranges and oversized lengths.

The actual layout and presets are embedded in
`<ps>:ui_settings_controller.sso` (107,254 bytes), alongside its view and
navigation definitions. `<ps>:ui_settings_layout.sso` itself is only a
23-byte generic object stub. `<ps>:ui_settings.ssolib` is a separate 16,075-byte
UI-class description resource. This resolves the name-to-description boundary;
it does not decode executable logic or prove an extension point.

Finding readable object descriptions does not establish that a loose
file can override them, that a newly named preset would be loaded, or that the
existing controller permits third-party rows.

Additional static metadata contains `UiSystem.GetLibListToLoad` and a reference
to `UiMovie.LoadLib`; these concern UI-library loading but do not establish a
script/controller registration API. The base view references a precreated UI
object. Reflection metadata describes field access, but establishes neither a
constructor nor a safe external ABI. The two inspected descriptor-pool modules
expose pool operations and internal initialization methods; they do not provide
a demonstrated public Settings-row factory or plugin entry point.

The remaining construction-path boundary is now specific: establish how the
engine resolves and instantiates `<ps>:ui_settings_controller.sso` and its
embedded typed objects, and whether that factory accepts original external
descriptions or offers a supported mutation interface. The serialized resource
contains no explicit external script or module load reference proving such a
route. Resource indexing alone cannot identify the native resource-manager
entry point or its calling contract. A runtime experiment becomes meaningful
only after identifying a concrete instance/factory and its calling and lifetime
contracts. There is still no implemented native Settings integration, and no
user interaction is presently needed to resolve this static engineering gap.
