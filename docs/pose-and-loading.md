# Truck pose metadata and loading feasibility

Observed 2026-11-03 against Steam build `26218478` and the release bundle fingerprint in [script inspection](script-inspection.md). This pass used offline file reads and public documentation only. No running-game interaction, attachment, memory scan, DLL loading, hook, script execution or installation change occurred.

## Result

The truck-marker metadata supplies a coherent **candidate** set of pose and map conversion types. It does not expose a callable extension API. The documented mod-content workflow has not yielded an SSL/HUD loading mechanism. A native DLL route has community precedent and a separately MIT-licensed loader, but compatibility, game-specific permission and runtime telemetry remain unverified. These findings do not justify replacing the synthetic adapter with guessed live data.

## Pose references corroborated

The inspector now offers `--preset truck-pose`, selecting nine modules totaling 126,115 bytes from the existing release bundle. Its cross-module comparison checks simple owner name, member name, slot and fully resolved inferred shape. It reports ambiguity, missing selected declarations and unresolved types instead of choosing a result. Names are not fully qualified by the inferred schema, so even a unique match is corroborating metadata rather than a verified linker resolution or call edge.

| Reference source | Referenced declaration / slot | Inferred result with no arguments | Declaration body field |
| --- | --- | --- | --- |
| Truck marker | `TrucksManager.GetCurrentTruck` / 49 | `Truck` | present |
| Truck marker | `TruckWrapper.GetTransformValue` / 15 | `Transform3d` | present |
| SSL truck wrapper | `Truck.GetTransformValue` / 241 | `Transform3d` | absent |
| Truck marker | `Transform3d.GetPosition` / 4 | `Vector3d` | absent |
| Truck marker | `Transform3d.GetForward` / 6 | `Vector3d` | absent |
| Truck marker | `Transform3d.GetRight` / 7 | `Vector3d` | absent |
| Truck marker | `MrCameraBase.GetTransformRef` / 18 | `Transform3d` | present |

The marker's `LevelData.ConvertWorldPositionToMinimap` reference also matches the slot-12 declaration with inferred `Vector3d -> Vector3d` shape. The wrapper's reference to `Truck.GetTransformValue` explains why an earlier short-string search did not find `GetPosition` in `game.truck`: the position accessor belongs to the transform type. This evidence does not prove the wrapper's actual execution path.

The direction indicator reference is `UiMinimapTruckdirectionindicator.SetOrientation3D`, slot 52, inferred three `float` parameters returning `void`. Its declaration is outside the nine-module selection and remains unmatched. The marker references both truck and camera transforms. We therefore cannot infer argument order, angular units, handedness, a heading formula, or whether an individual transform read describes the truck or the camera. Do not feed camera rotation directly into truck heading.

`math.transform_interface` also declares bodyless position/forward/right methods. **Body absence alone cannot distinguish an abstract interface from an engine implementation.** The installed editor descriptor `Sources/BinEditor/meta_desc_ssl.ps` independently names `TransformInterface` as `Transform3d`'s type and describes the direction indicator as a UI container from the minimap library. This descriptor is declarative text, not a discovered script compiler/loader. It was read, never executed or copied to the repository. SHA-256: `e4b5167662bac987090f82c554665aa88fa0917b470012e4f69fab8faa78aea0`.

Additional module boundaries, all in uncompressed bundle bytes:

| Module | Offset / bytes |
| --- | --- |
| `game.truck_wrapper.truck_wrapper` | `0x39a61c` / 4,582 |
| `game.truck_wrapper.truck_wrapper_ssl_truck` | `0x39e3b8` / 4,074 |
| `math.transform` | `0xc00601` / 6,324 |
| `math.transform_interface` | `0xc01eb5` / 1,023 |
| `game.game_camera.mr_camera_base` | `0x12076b` / 893 |

Across all nine modules, 435 member references produced 89 unique metadata candidates, 310 without a declaration in the selected set, and 36 with unresolved owner/type shapes. There were no ambiguous candidates in this selection. This is not 89 verified native functions; references include properties and the declaration collector covers methods only.

```powershell
python tools/inspect_script_metadata.py 'W:\SteamLibrary\steamapps\common\SnowRunner\preload\paks\client\initial.pak' --preset truck-pose --output build/pose-matches.local.json
python -m unittest discover -s tests -p 'test_script*.py' -v
```

All 23 Python tests pass, including new checks rejecting ambiguous owners, shape mismatches, unresolved types, and owner/slot mismatches. The real nine-module inspection completed successfully. C++ is unchanged; the earlier 86 CTest checks were not rerun. Reports and game-derived metadata dumps remain local and ignored.

## Loading assessment

Public primary sources checked on 2026-11-03:

- Saber's [mod usage guide](https://expeditions-guides.saber.games/usage_and_uploading_of_mods/usage_of_mods_from_mod_io/) documents subscription and activation through the game mod browser. Its [sample truck workflow](https://expeditions-guides.saber.games/truck_modding/getting_started/sample_mod_by_the_game/overview/) describes XML, meshes and related content. The [custom gameplay entities page](https://expeditions-guides.saber.games/custom_gameplay_entities/custom_gameplay_entities_intro/) lists cargo for SnowRunner; the richer entity types are for Expeditions. These pages do not document an arbitrary SSL compiler, module entry point or HUD callback. This is a bounded negative result, not proof that none exists.
- [SnowLoader at `35151eb`](https://github.com/maxpowa/SnowLoader/tree/35151eb63c4fff2e4ad021e7afb734f13931f78b) describes a native ASI host and DLL plugins. Its [host source](https://github.com/maxpowa/SnowLoader/blob/35151eb63c4fff2e4ad021e7afb734f13931f78b/src/host/dllmain.cpp) contains DXGI presentation and window-procedure hooks. Its [build file](https://github.com/maxpowa/SnowLoader/blob/35151eb63c4fff2e4ad021e7afb734f13931f78b/CMakeLists.txt) builds native libraries and fetches dependencies, including MinHook from a moving `master` branch. It is not evidence of a supported SSL loader. Its README's educational-use wording does not establish the source reuse rights needed here. No source was copied, built or executed.
- [Ultimate ASI Loader at `56fa0b0`](https://github.com/ThirteenAG/Ultimate-ASI-Loader/tree/56fa0b03920f2a71174126e03a05c77d48b74744) is a proxy-DLL loader with an explicit [MIT license](https://github.com/ThirteenAG/Ultimate-ASI-Loader/blob/56fa0b03920f2a71174126e03a05c77d48b74744/license). That makes it a possible separately reviewable loading component, not permission from the game publisher or evidence that our target process will load it correctly. No loader was downloaded or installed.
- The [Steam-linked EULA](https://store.steampowered.com//eula/1465360_eula_0) became readable using its double-slash URL. Article 3 includes restrictions on reverse engineering and modification absent publisher consent. This is the currently served text, not proof of which version Dawson accepted or an assessment of applicable exceptions. The [mod approval policy](https://expeditions-guides.saber.games/mod_approval_policy/) describes content verification; it does not specifically approve this native integration. No project-specific publisher authorization was found.

## Actionable boundary

Offline metadata research has now identified plausible pose types, but has not supplied a permitted script entry point, an executable ABI/address, a live object root or an update/render callback. Further unstructured string hunting will not validate those missing pieces.

The original [diagnostic DLL and isolated host](diagnostic-plugin.md) are now implemented and verified outside the game. This completes the standalone load/unload milestone with mock data. It does not include a game loader. Any future game integration still needs reviewed/pinned dependencies, a concrete build, exact game deployment manifest, behavior description and rollback plan before seeking action-time authorization.

No runtime operation has been requested for approval or attempted; none was denied. Do not interpret earlier permission for offline REA inspection as approval to place a proxy DLL in `Sources/Bin`, attach to `SnowRunner.exe`, or alter an SSL bundle. The next game-touching action must be reviewed explicitly, and any claim of publisher-supported loading still requires supporting evidence. The project remains a tested synthetic prototype with a bounded research report, not a live minimap.
