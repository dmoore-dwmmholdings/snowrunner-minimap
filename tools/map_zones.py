"""Read the observed counted static-zone table from local scene metadata.

Static geometry is not live objective state. A caller must join exact zone IDs
with an independently identified objective and map before calling them active.
Unsupported layouts fail closed; no nearby-string position guesses are used.
"""
import argparse
import json
import math
from pathlib import Path
import re
import struct
import zipfile

MAX_BYTES=64*1024*1024
MAX_TABLE_BYTES=256*1024
MAX_ZONES=1024


def _string(data,offset):
    if offset+2>len(data):raise ValueError('Truncated zone string.')
    length=struct.unpack_from('<H',data,offset)[0]
    end=offset+2+length
    if not 1<=length<=256 or end>len(data) or data[end-1]!=0:
        raise ValueError('Invalid zone string framing.')
    raw=data[offset+2:end-1]
    if any(c<32 or c>126 for c in raw):raise ValueError('Unsupported zone text encoding.')
    return raw.decode('ascii'),end


def _record(data,offset):
    if offset+28>len(data):raise ValueError('Truncated zone record.')
    shape,variant=struct.unpack_from('<2I',data,offset)
    if shape!=2 or variant not in (0,1):raise ValueError('Unsupported zone record variant.')
    x,z,dx,dz,height=struct.unpack_from('<5f',data,offset+8)
    if (not all(math.isfinite(v) for v in (x,z,dx,dz,height)) or
            max(abs(x),abs(z),abs(height))>20000 or not .98<=dx*dx+dz*dz<=1.02):
        raise ValueError('Invalid static-zone coordinates.')
    fields=[];offset+=28
    for _ in range(4):value,offset=_string(data,offset);fields.append(value)
    if not re.fullmatch(r'[A-Za-z0-9_]{1,127}',fields[0]):raise ValueError('Invalid zone identity.')
    tail_size=37 if variant else 45
    if offset+tail_size>len(data):raise ValueError('Truncated zone attributes.')
    flag=data[offset];values=struct.unpack_from('<9f' if variant else '<11f',data,offset+1)
    if flag not in (0,1) or not all(math.isfinite(v) for v in values):raise ValueError('Invalid zone attributes.')
    return {'id':fields[0],'x':x,'z':z,'kind':'static_zone','icon':fields[1],
            'label':fields[3]},offset+tail_size


def _groups_end(data,offset):
    # The trailing section is a counted list of named zone groups and counted
    # uint32 index arrays. Its semantics are not used to infer objective state.
    def count(limit):
        nonlocal offset
        if offset+4>len(data):raise ValueError('Truncated group count.')
        value=struct.unpack_from('<I',data,offset)[0];offset+=4
        if value>limit:raise ValueError('Group count exceeds bound.')
        return value
    for _ in range(count(128)):
        _,offset=_string(data,offset)
        for _ in range(count(128)):_,offset=_string(data,offset)
        for _ in range(count(64)):
            size=count(4096)*4
            if offset+size>len(data):raise ValueError('Truncated group index array.')
            offset+=size
    return offset


def parse_zones(data,bounds=None):
    if not 40<=len(data)<=MAX_BYTES:raise ValueError('Scene size exceeds supported bounds.')
    # Both the complete counted zone table and trailing named groups must parse
    # exactly through EOF. No partially decoded table is admitted.
    start=max(16,len(data)-MAX_TABLE_BYTES)
    candidates=[];attempts=0
    for match in re.finditer(rb'[A-Za-z][A-Za-z0-9_]{3,127}\x00',data[start:]):
        name=start+match.start()
        if name<34:continue
        count=struct.unpack_from('<I',data,name-34)[0]
        if (not 1<=count<=MAX_ZONES or struct.unpack_from('<I',data,name-30)[0]!=2 or
                struct.unpack_from('<I',data,name-26)[0] not in (0,1)):continue
        attempts+=1
        if attempts>128:raise ValueError('Too many candidate zone tables.')
        offset=name-30;records=[];identities=set()
        try:
            for _ in range(count):
                record,offset=_record(data,offset)
                if record['id'] in identities:raise ValueError('Duplicate zone identity.')
                identities.add(record['id']);records.append(record)
            if _groups_end(data,offset)!=len(data):continue
            candidates.append(records)
        except (ValueError,struct.error):continue
    if len(candidates)!=1:raise ValueError('Expected exactly one complete supported zone table.')
    return candidates[0]


def read_zones(level_pak,level):
    if not re.fullmatch(r'level_[a-z0-9_]{3,48}',level) or Path(level_pak).stem!=level:
        raise ValueError('Level archive identity mismatch.')
    with zipfile.ZipFile(level_pak) as archive:
        entries=archive.infolist()
        if len(entries)>200000:raise ValueError('Archive inventory exceeds bound.')
        found=[item for item in entries if item.filename=='[scene]/data_for_ssl.bin']
        if len(found)!=1 or not 40<=found[0].file_size<=MAX_BYTES:
            raise ValueError('Missing, duplicate or oversized scene member.')
        with archive.open(found[0]) as source:data=source.read(MAX_BYTES+1)
        return parse_zones(data)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client-dir',type=Path,required=True)
    parser.add_argument('--map-registry',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.map_registry.stat().st_size>1024*1024:parser.error('Map registry exceeds bound.')
    registry=json.loads(args.map_registry.read_text(encoding='utf8'))
    if registry.get('schema')!='srmm-map-registry-v1' or not 1<=len(registry.get('maps',[]))<=128:parser.error('Invalid map registry.')
    client=args.client_dir.resolve(strict=True);output=args.output.resolve()
    if len(client.parents)<3 or client.name.lower()!='client' or output.is_relative_to(client.parents[2]):parser.error('Output must stay outside the game installation.')
    if output.exists():parser.error('Output must be new.')
    catalog={'schema':'srmm-zone-catalog-v1','maps':{},'unavailable':[],'runtime_alignment_verified':False}
    for entry in registry['maps']:
        level=entry['map']
        try:catalog['maps'][level]=read_zones(client/(level+'.pak'),level)
        except (ValueError,OSError,zipfile.BadZipFile) as error:catalog['unavailable'].append({'map':level,'reason':str(error)})
    with output.open('x',encoding='utf8') as stream:json.dump(catalog,stream,indent=2)
    print(f"Read {sum(len(v) for v in catalog['maps'].values())} static zones from {len(catalog['maps'])} maps; {len(catalog['unavailable'])} unsupported layouts.")


if __name__=='__main__':main()
