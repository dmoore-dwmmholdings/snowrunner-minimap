"""Import installed map textures and scene-derived geometry into a private cache.

Original decoder only; no downloads, game changes or public asset distribution.
Unknown layouts are listed as unavailable. Scene-derived affine transforms remain
alignment previews until the running game has independently verified alignment.
"""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

from extract_map_texture import read_map
from map_catalog import discover_installed
from map_geometry import inspect_geometry,check_map_dimensions


def import_registry(client_dir, output_dir, levels=None):
    client_dir=Path(client_dir).resolve(strict=True)
    output_dir=Path(output_dir).resolve()
    if not client_dir.is_dir() or client_dir.name.lower()!='client':
        raise ValueError('Expected the installed preload/paks/client directory.')
    # All generated files must remain outside the installed game tree.
    if len(client_dir.parents)<3 or output_dir.is_relative_to(client_dir.parents[2]):
        raise ValueError('Private cache must be outside the game installation.')
    if output_dir.exists():raise ValueError('Use a new private output directory.')
    inventory=discover_installed(client_dir)
    if inventory['candidate_count']>128:
        raise ValueError('Installed candidate maps exceed the native registry limit of 128; update registry support before importing.')
    selected=set(levels) if levels is not None else None
    known={item['map'] for item in inventory['maps']}
    if selected is not None and (not selected or not selected<=known):
        raise ValueError('Requested map not present in installed playable-level inventory.')
    output_dir.mkdir(parents=True,exist_ok=False)
    registry={'schema':'srmm-map-registry-v1','maps':[],'unavailable':[],
              'runtime_alignment_verified':False,'coverage':inventory}
    total=0
    for item in inventory['maps']:
        level=item['map']
        if selected is not None and level not in selected:continue
        try:
            if item['status']!='ready_for_import':
                registry['unavailable'].append({'map':level,'reason_code':item['status'],'reason':item['reason']})
                print(f"Unavailable {level}: {item['reason']}",flush=True)
                continue
            if len(item['sources'])!=1:raise ValueError('Ambiguous texture source.')
            bounds,evidence=inspect_geometry(client_dir/(level+'.pak'),level)
            texture=read_map(Path(item['sources'][0]['archive']),level)
            check_map_dimensions(bounds,(texture.width,texture.height))
            payload=texture.dds()
            if total+len(payload)>2*1024*1024*1024:raise ValueError('Private cache exceeds 2 GiB bound.')
            asset=output_dir/(level+'.local.dds')
            with asset.open('xb') as target:target.write(payload)
            total+=len(payload)
            registry['maps'].append({
                'map':level,'map_path':str(asset),
                'map_sha256':hashlib.sha256(payload).hexdigest(),
                'affine':bounds.affine,
                # Nominal 1 metre per pixel along world X. The renderer must use
                # the affine basis to preserve aspect on rectangular terrain.
                'uv_per_pixel':1/bounds.width,'uv_per_pixel_v':1/bounds.depth,
                'texture_v_flipped':True,
                'geometry':'alignment_preview','evidence':evidence})
            print(f'Imported {level}: {bounds.width:g} x {bounds.depth:g} metres (alignment preview).',flush=True)
        except (OSError,ValueError,KeyError,zipfile.BadZipFile) as error:
            registry['unavailable'].append({'map':level,'reason_code':'import_failed','reason':str(error)})
            print(f'Unavailable {level}: {error}',flush=True)
    registry['asset_bytes']=total
    registry['coverage']['imported_count']=len(registry['maps'])
    registry['coverage']['selection']='all_installed_candidates' if selected is None else 'explicit_subset'
    registry['coverage']['all_installed_candidates_imported']=(selected is None and
        inventory['inventory_complete'] and len(registry['maps'])==inventory['candidate_count'])
    path=output_dir/'map-registry.local.json'
    with path.open('x',encoding='utf8') as target:json.dump(registry,target,indent=2)
    return path,registry


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client-dir',type=Path,required=True)
    parser.add_argument('--output-dir',type=Path,required=True)
    parser.add_argument('--map',action='append',dest='levels')
    args=parser.parse_args()
    path,registry=import_registry(args.client_dir,args.output_dir,args.levels)
    print(f"Registry: {path}; {len(registry['maps'])} imported, {len(registry['unavailable'])} unavailable.")
    print(f"Coverage: {len(registry['maps'])}/{registry['coverage']['candidate_count']} installed candidate maps; live alignment remains unverified.")
    if not registry['maps']:parser.exit(1,'No map met the geometry and texture checks.\n')


if __name__=='__main__':main()
