# Offline minimap and vehicle reference inspection

Observed 2026-11-03 on Steam build `26218478`. This is a candidate integration investigation, not a working mod or a supported script API. The running game was not interacted with during this pass. No process attachment, live-memory scan, script execution, unpacking/protection bypass or game-file changes were performed.

## Bounded native inspection

REA 6.5.0 `inspect-with-objdump ... section_headers` completed without starting Ghidra. Evidence ID: `ev_06beab63197af4450d45425683e36b22feccd3c932865abb635912d5846dec50`; raw result stays in `build/rea-sections.local.json`. The executable hash matches the [Windows checkpoint](windows-inspection.md). The observed entry point is in `.bind`, outside `.text`.

A separate task-local byte search found minimap initializer/type names in `.rdata` and `.data`. A bounded GNU objdump sample of the first 256 `.text` bytes did not yield coherent ordinary x64 code. A simple RIP-relative LEA/MOV candidate search found no references to the 42 selected strings. These are limited negative observations, not proof of encryption, a particular protection product, or absent functionality. Do not turn those bytes into guessed function addresses or repeat the broad Ghidra run. No unpacker was used.

One false lead was rejected: nearby literals around `targetPosition` describe pivots, constraints and motors, so that string alone is not evidence of the player's truck position.

## Script container observations

`preload/paks/client/initial.pak` is a readable ZIP with 12,171 entries: 12,152 XML files, 13 string files, three script bundles and three other entries. The release bundle member is `[ssl_cache]/initial_release.sslbundle`, 13,230,511 uncompressed bytes, SHA-256 `22ce203da583faba881e148bec83af1a2e87d37c47328c6fb59e829937df8ad8`. The archive entry is not marked encrypted. The separate `shared.pak` inventory contained no filenames matching minimap, HUD, script or `.ssl`; that filename search does not establish absence of relevant content.

The observed S3DB variant contains a length-prefixed name table with 2,284 unique module names and a corresponding size table. Their lengths cover the remaining file exactly; all module blocks begin with the observed SSLB header. The original inspector validates this framing, not the bytecode grammar or any internal checksum. It rejects unsupported versions, truncated tables, duplicate names and inconsistent sizes.

The following exact module boundaries and short identifier candidates were found. All offsets below refer to uncompressed bundle bytes, **not game process addresses**.

| Module | Offset / size | Selected identifier candidates |
| --- | --- | --- |
| `game.game_states.game_state_minimap.minimap_info_provider` | `0x18568d` / 639 | `SetMinimap`, `MinimapPointInfo` |
| `game.minimap` | `0x1dfb73` / 2,293 | `ConvertMinimapPositionToUI`, `ConvertUIPositionToMinimap`, `minimapPos` |
| `game.truck_manager.trucks_manager` | `0x38f6f3` / 21,146 | `GetCurrentTruck`, `GetPosition`, `GetLevelName`, `OnCurrentTruckChanged`, `HandleOnLevelLoaded` |
| `ui.components.minimap.minimap_camera_controller` | `0x6e57d9` / 18,897 | `mapMaxX`, `mapMaxZ`, `rotation` |
| `ui.components.minimap.minimap_compass_controller` | `0x6ea2e9` / 2,110 | `GetActiveCamera`, `GetPosition`, `rotation` |
| `ui.components.minimap.objects.minimap_truck_controller` | `0x7452d8` / 22,791 | `GetCurrentTruck`, `GetPosition`, `ConvertWorldPositionToMinimap`, `directionIndicator` |
| `game.level_data.level_data` | `0x1caf78` / 1,865 | `ConvertWorldPositionToMinimap` |

The follow-up `game.level_data.level_data` record contains the world-to-map conversion candidate. `game.truck` was also inventoried at `0x32296b` (63,417 bytes); the short-identifier search did not find `GetPosition` there. That negative result does not exclude an inherited or native position accessor.

Identifier candidates use an observed short-string byte pattern. They do not establish definitions versus references, function signatures, call order, units, coordinate handedness, heading conventions, ownership or an externally callable API. In particular, the compass module references a camera; camera orientation must not be substituted for truck heading. The name “minimap” may describe the game's existing full map screen, not an always-visible HUD element.

## Reproduce without touching the running game

Python 3.9+ and the standard library are sufficient; no extra packages are needed. Supply the installation's archive path. The output destination must be new; the inspector refuses to overwrite an existing file. It reads one bounded ZIP member into memory and writes only the requested JSON report. It never extracts or executes the game scripts.

```powershell
python tools/inspect_script_bundle.py 'W:\SteamLibrary\steamapps\common\SnowRunner\preload\paks\client\initial.pak' --output build/script-bundle.local.json
python -m unittest discover -s tests -p test_script_bundle.py -v
```

Eight synthetic unit tests pass, including rejection of every truncated prefix of the valid fixture. The inspector also completed against the installed archive, selecting six default modules plus two explicitly requested spatial-type modules. Report metadata and identifier candidates remain in ignored `build/script-bundle.local.json` and `build/script-spatial-types.local.json`; no game bytecode, images or assets are checked in. These eight tests are separate from the previous 86 CTest checks; the C++ prototype was unchanged in this pass.

## Exact next step

Start with the 639-byte info-provider, 1,865-byte level-data and 2,293-byte `game.minimap` modules. Inspect their SSLB metadata framing offline to distinguish type declarations, native bindings and references. Then inspect the 22,791-byte truck-marker module for the position-to-map conversion and direction-indicator inputs. Determine whether a documented, permitted script extension/load path exists before proposing any installation.

This gives a much smaller target than the 46.8 MB executable and may support a script-level route. It is an inference from the container and identifiers, not a verified integration mechanism. There is still no live telemetry reader, native render hook, reliable game offset, script loader or in-game minimap in this repository. If runtime attachment or a game modification becomes necessary, stop and present its exact target, behavior and rollback plan for approval first. No additional approval is needed for the offline parsing step above.
