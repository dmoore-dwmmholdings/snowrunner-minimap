# Feed lifetime and combined session launcher

The telemetry source is a separate, explicitly selected read-only process. The
native renderer intentionally hides a packet older than 750 ms. F9 changes only
visibility; it cannot restore a stopped publisher. Keep that stale-data behavior.

`candidate_feed.py --until-game-exit` remains active until the selected game
handle exits, an exact-run stop request arrives, or its maximum duration expires.
The default maximum is eight hours (override `--seconds`, 1–28800). Ordinary
diagnostic calls still default to 30 seconds and remain limited to 600 seconds.
It opens no replacement game when a PID is reused. A byte-range lock admits one
publisher per output directory. Explicit resume retains PID/session/build and
sequence; `--allow-map-change` additionally permits a different canonical map in
the last packet of that same session.

The optional `--record <new-path>` writes a local JSONL recording in the publisher
process, normally at five records per second, capped at 64 MiB. A recording error
is reported in status and does not stop telemetry. Recordings contain pose data
and must remain private. The normal stop path emits an unavailable packet. A
forced process termination cannot run cleanup; packet freshness still hides it.

`status.local.json` updates every half-second. It contains schema
`srmm-feed-status-v1`, game `pid`, `publisher_pid`, `session`, random per-start
`run`, `state`, current sampled `map`, sequence, `tick_ms` (`GetTickCount64`),
`pose_available`, `record_error`, and `maximum_seconds`. It contains no vehicle
coordinates. A `stop.local.json` request must exactly match schema
`srmm-feed-stop-v1`, `pid`, `session`, and `run`. A request from an earlier run
cannot stop its replacement. These files are local control messages, not a
cross-user authentication mechanism.

`feed_launcher.py` is an ordinary user-launched Windows control window. It starts
only original repository modules, displays heartbeat/errors, and requests a clean
stop on close. It is not a service, scheduled task, or mechanism to escape an
automation job. Launch it normally from Windows so its lifetime is independent
of a short-lived tool session. `Launch-Minimap.ps1 -Manifest <absolute-path>` uses
the installed `pythonw.exe` to open that window without a terminal. It does not
change script execution policy or security settings.

The private UTF-8 launch manifest is at most 16 KiB:

```json
{
  "schema": "srmm-launch-v1",
  "pid": 12345,
  "session": 123456789,
  "map": "level_example_01",
  "layout": "C:\\Private\\layout.local.json",
  "output_dir": "C:\\Private\\feed",
  "config": "C:\\Private\\session.local.ini",
  "registry": "C:\\Private\\maps.local.json",
  "record": true,
  "seconds": 28800
}
```

`config` and `registry` are optional together and enable the original map watcher.
The optional `save` names one explicit save slot; `saved_waypoints_preview: true`
is required with it. The resulting destination source is labeled **Saved
destinations (alignment preview)**, not live waypoints or active objectives. The
optional `zone_catalog` supplies locally parsed zone coordinates for saved tracked
objectives and requires the same explicit save/preview selection. These are saved
state, not live active-objective telemetry. The launcher waits for the identified feed's running heartbeat before starting
companions, which bind their lifetime to that run. No save slot is guessed.

The existing native session configuration must already match the supplied
session and packet path. The launcher does not install DLLs, modify game files,
restart SnowRunner, choose calibration, or discover/attach to arbitrary PIDs.
The separate control window manages lifetime; it is not the requested in-game
settings panel.

Owned tests exercise game exit, stop-before-sample, exception cleanup, deadline,
stale stop tokens, resume after map transition, manifest/path validation and
heartbeat identity. They do not establish live menu detection or calibration.
