# Local maps and geometry

`tools/import_map_registry.py` discovers the two installed shared-texture archives,
imports supported map textures into a new private directory, and reads each
matching level archive. It does not download images or modify the installation.
The generated DDS files and registry are local-only assets, never repository
content.

```powershell
python tools/import_map_registry.py --client-dir "<SnowRunner>\preload\paks\client" --output-dir "<private-cache>\maps"
```

Use repeated `--map level_...` arguments to import a subset. The output directory
must be new and outside the game installation. Total DDS output is bounded to
2 GiB. An unavailable or ambiguous map is recorded with its reason and omitted
from the admitted registry.

The observed scene record contains width, depth, world X/Z origin and terrain
block counts. The importer searches only the first 64 KiB of
`[scene]/data_for_ssl.bin` and requires exactly one record whose metre dimensions
agree with its block counts and an independently parsed height-texture header.
The header must describe the observed one-mip R16 layout, with 48 terrain cells
per block. Each block is 24 metres. The actual map image must then match the
full terrain cell dimensions. A resized or cropped image is rejected.

The affine transform uses the recorded origin, including noncentered origins;
it does not assume every map is a 2016-metre square. Rectangular maps supply
separate `uv_per_pixel` and `uv_per_pixel_v` values so one screen pixel represents
the same world distance along either axis. These optional fields need the
corresponding native renderer support.

Imported maps retain `geometry=alignment_preview` and a private evidence record.
Reading consistent metadata is not the same as verifying imagery alignment in
the running game. The evidence includes source member CRC/size, prefix/header
hashes, the record offset, dimensions and origin. The initial live test should
check landmarks in two separated positions and a rectangular map transition.

The bounded parser and importer are original implementations. The terrain-unit
cross-check is supported by [Saber's terrain creation documentation](https://expeditions-guides.saber.games/map_modding/creating_a_map/terrain/creation_of_terrain/).
The independent [MIT SnowrunnerMapConverter source](https://github.com/skibidius/SnowrunnerMapConverter/blob/main/snowrunner_map_converter.py)
also describes the 48-cell terrain grid. No third-party code or game assets are
copied into this repository.

## Static destination geometry

`tools/map_zones.py` reads the local scene's complete counted zone table, then
validates its trailing counted group structure through end-of-file. Both observed
box and round zone variants are supported. String lengths, record counts,
coordinates, identities and all boundaries are checked. Unknown variants,
duplicate identities and partial tables disable that map's zone import.

```powershell
python tools/map_zones.py --client-dir "<SnowRunner>\preload\paks\client" --map-registry "<private-cache>\maps\map-registry.local.json" --output "<private-cache>\zone-catalog.local.json"
```

The private result uses `srmm-zone-catalog-v1`, with `maps` mapping each canonical
map ID to records containing `id`, `x`, `z`, `kind`, `icon` and `label`. The kind
is always `static_zone`. An objective adapter must join an independently known
active objective to the exact map and zone ID. A zone's mere existence does not
mean it is discovered, active or reachable. This parser does not infer gateway
routes from names and does not project a different map's coordinates onto the
current map.
