"""Bounded local map discovery. No extraction, downloads or game writes.

Discovery establishes asset names only. Unknown calibration must never inherit
another map's calibration. Use import_map_registry to join assets with independently checked scene geometry.
"""
import argparse,json,re,zipfile
from pathlib import Path

LEVEL = re.compile(r'level_[a-z0-9_]{3,48}')
MAX_ARCHIVES = 256
MAX_MEMBERS = 200000


def discover_installed(client_dir):
    """Audit every installed level against bounded texture-container indexes.

    Level identity comes from the archive filename, not a loose texture suffix:
    terrain height/water/extrude maps also end in ``_map.pct``. No payload is
    extracted here and archive order is never treated as game override priority.
    """
    root = Path(client_dir).resolve(strict=True)
    if not root.is_dir() or root.name.lower() != 'client':
        raise ValueError('Expected the installed preload/paks/client directory.')
    paths = []
    for path in root.glob('*.pak'):
        if len(paths) >= MAX_ARCHIVES:
            raise ValueError('Installed archive inventory exceeds 256; no complete coverage claim is possible.')
        paths.append(path)
    paths.sort(key=lambda p: p.name.lower())
    levels = {}
    containers = []
    seen = set()
    for path in paths:
        folded = path.name.lower()
        if folded in seen:raise ValueError('Case-ambiguous installed archive names.')
        seen.add(folded)
        if LEVEL.fullmatch(path.stem):levels[path.stem] = path
        if (LEVEL.fullmatch(path.stem) or path.name == 'main_menu_textures.pak' or
                re.fullmatch(r'shared_textures(?:_[a-z0-9]+)*\.pak', path.name)):
            if path.resolve().parent != root or path.is_symlink():
                raise ValueError('Map archive must be a direct regular file in the selected installation.')
            containers.append(path)
    if not levels:raise ValueError('No canonical installed level archives found; verify the selected game directory.')
    excluded = [{'map':level,'reason_code':'non_gameplay_scene',
                 'reason':'Main-menu scene; not a playable-map coverage claim.'}
                for level in levels if level.startswith('level_main_menu_')]
    candidates = {level:[] for level in levels if not level.startswith('level_main_menu_')}
    wanted = {f'[textures]/pct/{level}_map.pct':level for level in candidates}
    errors = []
    invalid = []
    for path in containers:
        try:
            with zipfile.ZipFile(path) as archive:
                entries = archive.infolist()
                if len(entries)>MAX_MEMBERS:raise ValueError('Archive member inventory exceeds bound.')
                for entry in entries:
                    level = wanted.get(entry.filename)
                    if level is None:continue
                    if not 98 <= entry.file_size <= 96*1024*1024 or entry.flag_bits & 1:
                        invalid.append({'map':level,'archive':path.name,'member':entry.filename,
                                        'reason_code':'unsupported_texture_entry'})
                    else:candidates[level].append({'archive':str(path.resolve()),'member':entry.filename})
        except (OSError,ValueError,zipfile.BadZipFile) as error:
            errors.append({'archive':path.name,'reason':str(error)})
    maps = []
    for level,sources in sorted(candidates.items()):
        if any(item['map']==level for item in invalid):
            status,reason = 'unsupported_texture_entry','A matching texture is encrypted or outside size bounds; update importer support before using it.'
        elif len(sources)>1:
            status,reason = 'ambiguous_texture','Multiple exact textures exist; game override priority is unknown. Verify installation or provide supported precedence evidence.'
        elif not sources:
            status,reason = 'missing_texture','No exact minimap texture found in supported containers; verify installed content or add a supported texture format.'
        elif errors:
            status,reason = 'incomplete_inventory','An archive could not be inspected; duplicates cannot be ruled out. Repair the named archive and retry.'
        else:status,reason = 'ready_for_import','Texture found; geometry, format and live alignment still require validation.'
        maps.append({'map':level,'level_archive':str(levels[level].resolve()),'sources':sources,
                     'status':status,'reason':reason})
    return {'schema':'srmm-installed-map-coverage-v1','maps':maps,'excluded':excluded,
            'archive_errors':errors,'invalid_entries':invalid,'texture_containers':len(containers),
            'level_archive_count':len(levels),'candidate_count':len(maps),
            'ready_count':sum(item['status']=='ready_for_import' for item in maps),
            'inventory_complete':not errors,'runtime_alignment_verified':False}


def discover(paks):
    if not 1 <= len(paks) <= 128: raise ValueError('Expected 1..128 local archives.')
    found={}
    for path in paks:
        with zipfile.ZipFile(path) as archive:
            entries=archive.infolist()
            if len(entries)>200000: raise ValueError('Archive inventory exceeds bound.')
            for entry in entries:
                match=re.fullmatch(r'\[textures\]/pct/(level_[a-z0-9_]{3,48})_map\.pct',entry.filename)
                if match and 98<=entry.file_size<=96*1024*1024:
                    level=match[1]
                    found.setdefault(level,[]).append({'archive':str(Path(path).resolve()),'member':entry.filename})
    return {'schema':'srmm-map-discovery-v1','maps':[
        {'map':level,'sources':sources,'status':'ambiguous' if len(sources)!=1 else 'needs_import_and_calibration'}
        for level,sources in sorted(found.items())]}


class MapSelection:
    """Fail closed immediately on an identity change; admit only explicit matches."""
    def __init__(self):self.map=None;self.generation=0;self.asset=None
    def select(self,level):
        if level!=self.map:
            self.map=level;self.generation+=1;self.asset=None
        return self.generation
    def admit(self,level,generation,asset,calibrated=False):
        if level!=self.map or generation!=self.generation or not calibrated or asset is None:return False
        self.asset=asset;return True


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('paks',nargs='+',type=Path);parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    data=discover(args.paks)
    with args.output.open('x',encoding='utf-8') as output:json.dump(data,output,indent=2)
    print(f"Discovered {len(data['maps'])} map identities; import/calibration pending.")
if __name__=='__main__':main()
