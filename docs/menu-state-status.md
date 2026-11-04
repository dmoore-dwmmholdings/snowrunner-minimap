# Menu-state adapter status

The current native layer does **not** yet have a verified SnowRunner menu-state
signal. Pose availability, a stationary vehicle, keyboard Escape/M history and
Windows cursor visibility are not substitutes for that signal. They can hide a
working minimap or leave it visible over controller-operated menus.

The read-only investigation found typed native GUI-system and game-input-manager
roots in the fingerprinted executable's bounded data section. Their sampled
prefixes, and the sampled session prefix, remained identical between observed
Functions and full-map screens. Root identity therefore does not establish the
active screen. No script-runtime root was found by the same bounded RTTI inventory.

Offline script metadata contains candidate `UiSystem` menu-enter/menu-leave
events, `ingameMenuCtrl` and `mainMenu` fields, `GameStatesManager.curState`, and
`HudStateManagerMr.currentState`. Metadata slots are not runtime byte offsets;
these names do not establish a callable ABI or authorize invoking engine code.
No game functions were called, executable pages changed, debugger attached,
security settings changed, or protection mechanisms bypassed in this investigation.

## Required verification

A candidate must distinguish observed gameplay, pause, Settings, full map and
return to gameplay, including controller input. Repeated gameplay captures before
and after menu transitions are required to reject changing counters and unrelated
state. A fresh process/map transition also needs verification before describing the
source as generally supported. Unknown/stale/mismatched state must not assert gameplay or admit the minimap when a UI-state source is configured.

The bounded private capture helper records at most the executable's accepted
data section and three 1 KiB candidate windows. Captures and raw addresses remain
outside the repository. This helper is diagnostic evidence collection, not a
runtime menu detector. A scalar that correlates with one menu transition is not
automatically promoted to a gameplay flag.

The native F10 display panel is independently editable in the foreground; it advises pausing first and does not consume game input. Menu detection is advisory for that panel, not a requirement for reversible display edits.

The combined layer accepts an optional `ui_state_path` carrying the original 128-byte `SRMMUI01` contract. Its identity/freshness gate hides the minimap for verified menu, unknown, missing, stale or mismatched state; fresh gameplay restores it without a key latch. With no configured source, automatic menu suppression remains unavailable.

`tools/ui_state_feed.py` requires an explicitly verified, build-specific private profile. No profile is bundled or enabled. It performs only bounded read-only queries, publishes unknown on transitional data, and stops when its selected game exits or its duration ends. Packet/profile fixtures validate this plumbing, not actual SnowRunner menu detection.
