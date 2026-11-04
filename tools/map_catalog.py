"""Bounded local map discovery. No extraction, downloads or game writes.

Discovery establishes asset names only. Unknown calibration must never inherit
another map's calibration. Use import_map_registry to join assets with independently checked scene geometry.
"""
import argparse,json,re,zipfile
from pathlib import Path


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
