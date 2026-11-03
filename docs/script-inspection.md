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

## Metadata follow-up: declarations versus references

The three small modules above were successfully read with the original `tools/inspect_script_metadata.py` inspector. After their 16-byte SSLB header, the payload follows protobuf-style tagged wire framing. The final header word equals the payload length in these modules. The preceding word is **unidentified**, not a validated checksum. No official schema was found or assumed.

Observed root field 6 holds a string table; fields 5, 7 and 8 appear to hold type, declaration and function records. Root field 4 contains imported type/member references. These role names and the type-shape interpretation are inferred. The inspector validates lengths, wire types, indices and agreement between declaration/function names. It treats function field 5 as an opaque body field and reports only its presence. It does not decode instructions, follow calls, emit game code or compute runtime addresses.

| Module | Method declarations | With body field | Member references |
| --- | ---: | ---: | ---: |
| `minimap_info_provider` | 2 | 2 | 1 |
| `game.level_data.level_data` | 5 | 2 | 5 |
| `game.minimap` | 20 | 2 | 0 |

The strongest cross-module check is the info-provider's `GetNavigationPath` **reference**, whose owner resolves to `Minimap`, slot 13. It agrees with the `game.minimap` **declaration** on owner, name, slot and inferred function shape: no parameters and an array of `MinimapPointInfo`. The info-provider also declares its own `GetNavigationPath` at slot 6, with a body field; these are distinct records. Their relationship is metadata evidence, not a decoded call edge.

Selected method shapes inferred from the records:

| Owner / member | Inferred parameters → result | Body field |
| --- | --- | --- |
| `MinimapInfoProvider.SetMinimap` | `Minimap` → `void` | present |
| `Minimap.GetNavigationPath` | none → array of `MinimapPointInfo` | absent |
| `Minimap.GetSize` | none → `Vector2d` | absent |
| `Minimap.ConvertUIPositionToMinimap` | `Vector2d` → `MinimapPointInfo` | absent |
| `Minimap.ConvertMinimapPositionToUI` | `Vector3d` → `Vector2d` | absent |
| `LevelData.ConvertWorldPositionToMinimap` | `Vector3d` → `Vector3d` | absent |
| `LevelData.ConvertMinimapPositionToWorld` | `Vector3d` → `Vector3d` | absent |

All 21 bodyless method declarations in these modules have member-kind value 1. All six with body fields have member-kind value 2. This is consistent with externally implemented bindings versus script implementations, but **does not prove native implementation, export accessibility, or a supported extension API**. The conversion methods' bodies are not available here; coordinate conventions, units and runtime object lifetimes remain unknown. `Minimap.Open` also takes `LevelData` and a string in the inferred shape, supporting investigation of map-screen lifecycle rather than assuming an always-active HUD service.

```powershell
python tools/inspect_script_metadata.py 'W:\SteamLibrary\steamapps\common\SnowRunner\preload\paks\client\initial.pak' --output build/script-metadata.local.json
python -m unittest discover -s tests -p 'test_script*.py' -v
```

All 18 synthetic Python tests pass: the original eight framing tests plus ten metadata tests covering truncation, invalid wire fields, invalid indices, name disagreement, body presence and opaque-body handling. Inspection completed against the three real modules; the cross-module match above was checked separately against the resulting JSON. The C++ code is unchanged, so its earlier 86 checks were not rerun. Reports stay local/ignored; only original tooling, synthetic fixtures and this factual summary are committed.

## Exact next step

Inspect metadata in the 22,791-byte truck-marker module and its imported owner types to resolve `GetPosition` and direction-indicator inputs. This can distinguish truck pose from camera rotation and locate the declared owner of an inherited accessor without interpreting instructions. Separately establish a documented, permitted script extension/load path and its lifecycle constraints. No additional approval is needed for this offline research.

The three-module pass is complete. There is still no verified telemetry reader, native render hook, callable engine address, script loader or functioning in-game minimap. The bodyless conversion declarations alone cannot implement the adapter. If runtime attachment or a game modification becomes necessary, stop and present its exact target, behavior and rollback plan for approval first; none was attempted in this pass.
