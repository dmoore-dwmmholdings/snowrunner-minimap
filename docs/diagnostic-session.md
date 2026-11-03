# Diagnostic rendering session

The opt-in factory-route build now connects its swap-chain wrapper to the existing
background file reader and render-thread view. The default bridge still draws only
in the owned fixture. Game drawing additionally requires the existing executable
fingerprint check and an explicit private session configuration. This is an
experimental native integration, not a supported game plugin API.

`%LOCALAPPDATA%/SnowRunnerMinimap/session.local.ini` is read once on a new original
controller thread after swap-chain creation, outside DllMain and Present. Missing
or invalid configuration leaves drawing disabled. It must contain exactly these
keys, one `key=value` per line, UTF-8, at most 16 KiB:

- `version=1`, `enabled=1`
- `map_path`, `packet_path`: absolute local paths accepted by the existing reader
- `map_sha256`: lowercase SHA-256 of a privately imported DDS
- `session`: nonzero decimal uint64, matching `candidate_feed.py --session`
- `map`: canonical level identifier
- `affine`: six finite numbers mapping world X/Z to north-up U/V
- `uv_per_pixel`: positive map scale
- `texture_v_flipped`: 0 or 1
- `geometry`: `alignment_preview` or `measured`
- `viewport`: left, top and diameter in pixels

Do not mark geometry measured without actual alignment evidence. Preview mode
has a magenta rim and a distinct `ready_alignment_preview` packet result. The
ordinary candidate gate continues rejecting unmeasured geometry by default;
preview still checks identity, map, sequence, direction validity and freshness.
Neither mode promotes the source into the production FrameController.

An explicitly enabled session displays only when fresh matching telemetry is
available and its game window is foreground. F9 toggles it; Escape and M hide it
when observed. This is a manual diagnostic visibility mechanism, **not a verified
gameplay/menu-state detector**. Key polling can miss short taps. Garage/loading/
truck-switch behavior and calibration must be checked before release.

The controller owns ingestor start/stop and publishes immutable snapshots. Present
does no file I/O or worker waits. Swap destruction revokes admission and signals
the controller without joining it. Worker-owned state and pinned original code
remain valid until cancellation completes or the process exits. Drawing uses the
existing borrowed-backbuffer renderer and preserves immediate-context state;
concurrent Present calls skip the optional layer rather than sharing its view.

The build does not start a telemetry process. The bounded read-only candidate feed
must be explicitly started against the current PID, with its matching session,
prior layout report, map and a new private output directory. It emits unavailable
on normal stop; a stalled feed becomes stale within 750 ms. Do not share imported
game assets, session configs, raw telemetry or save backups in this repository.

Validation: eight Release CTest suites and 47 Python tests pass. These automated
checks use owned fixtures and synthetic packets; they do not establish live map
alignment, movement or gameplay-transition correctness.

The publisher retries ordinary Windows sharing/access conflicts for at most 250 ms.
An explicit --resume reopens only the same PID/session/map/build and continues its
sequence. A Windows byte-range lock prevents simultaneous publishers for that
directory. It never disables scanners or changes file permissions.
