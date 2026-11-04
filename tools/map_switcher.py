"""Follow a local diagnostic packet and select an explicitly calibrated map.

No process access or game writes. Assets stay local. Unknown maps disable the
display; returning to a registered map restores it. Run with the generation-aware
native layer. A preview entry remains a preview, never a measured calibration.
"""
import argparse
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import tempfile
import time

from probe_live_pose import EXPECTED_SHA256
from session_config import config_lock
from feed_lifecycle import read_json


def load_registry(path):
    if path.stat().st_size > 1024*1024: raise ValueError('Registry exceeds size limit.')
    data=json.loads(path.read_text(encoding='utf8'))
    if data.get('schema')!='srmm-map-registry-v1' or not isinstance(data.get('maps'),list) or not 1<=len(data['maps'])<=128:
        raise ValueError('Invalid bounded map registry.')
    result={}
    for entry in data['maps']:
        level=entry['map'];affine=entry['affine'];scale=entry['uv_per_pixel']
        if not re.fullmatch(r'level_[a-z0-9_]{3,48}',level) or level in result:raise ValueError('Invalid or duplicate map.')
        if len(affine)!=6 or not all(type(v) in (int,float) and math.isfinite(v) for v in affine):raise ValueError('Invalid affine.')
        if abs(affine[0]*affine[4]-affine[1]*affine[3])<1e-14:raise ValueError('Singular affine.')
        if type(scale) not in (int,float) or not math.isfinite(scale) or not 0<scale<=1:raise ValueError('Invalid scale.')
        if entry['geometry'] not in ('measured','alignment_preview') or type(entry['texture_v_flipped']) is not bool:raise ValueError('Explicit geometry/texture orientation required.')
        asset=Path(entry['map_path'])
        if not asset.is_absolute() or not 144<=asset.stat().st_size<=67108992:raise ValueError('Expected bounded absolute local DDS.')
        digest=entry['map_sha256']
        if not re.fullmatch('[a-f0-9]{64}',digest) or hashlib.sha256(asset.read_bytes()).hexdigest()!=digest:raise ValueError('Map asset fingerprint mismatch.')
        result[level]={
            'map':level,'map_path':str(asset),'map_sha256':digest,
            'affine':' '.join(str(v) for v in affine),'uv_per_pixel':str(scale),
            'texture_v_flipped':str(int(entry['texture_v_flipped'])),'geometry':entry['geometry']}
        vscale=entry.get('uv_per_pixel_v',scale)
        if type(vscale) not in (int,float) or not math.isfinite(vscale) or not 0<vscale<=1:raise ValueError('Invalid vertical scale.')
        result[level]['uv_per_pixel_v']=str(vscale)
    return result


def read_fields(path):
    raw=path.read_bytes()
    if not 0<len(raw)<=16384 or b'\0' in raw:raise ValueError('Invalid session file size/content.')
    pairs=[line.split('=',1) for line in raw.decode('utf8').splitlines() if line]
    fields=dict(pairs)
    if len(fields)!=len(pairs) or fields.get('version')!='1':raise ValueError('Invalid session fields.')
    return raw,fields


def packet_map(data,pid,session,now):
    if len(data)!=256 or data[:8]!=b'SRMMTEL1':return None
    version,size,flags,source,seq,tick,identity=struct.unpack_from('<4I3Q',data,8)
    if (version!=1 or size!=256 or flags!=1 or source!=pid or identity!=session or not seq or tick>now or now-tick>750 or
        data[112:144]!=bytes.fromhex(EXPECTED_SHA256) or any(data[192:])):return None
    name,sep,padding=data[48:112].partition(b'\0')
    if not sep or any(padding) or not re.fullmatch(rb'level_[a-z0-9_]{3,48}',name):return None
    values=struct.unpack_from('<6d',data,144)
    if not all(math.isfinite(v) for v in values) or max(abs(v) for v in values[:3])>20000 or not .98<=sum(v*v for v in values[3:])<=1.02:return None
    return name.decode('ascii')


def choose(fields,registry,level):
    result=dict(fields)
    # Missing/stale pose already hides through the native gate; do not churn assets.
    if level is None:return result
    if level not in registry:result['enabled']='0'
    else:result.update(registry[level],enabled='1')
    return result


def replace_if_unchanged(path,before,fields):
    with config_lock(path):
        return _replace_locked(path,before,fields)


def _replace_locked(path,before,fields):
    payload=''.join(f'{k}={v}\n' for k,v in fields.items()).encode('utf8')
    if len(payload)>16384:raise ValueError('Session exceeds size limit.')
    if payload==before:return False
    temporary=None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent,prefix='map-next-',suffix='.local.ini',delete=False) as output:
            temporary=Path(output.name);output.write(payload)
        # Preserve display edits observed during this iteration; retry next time.
        if path.read_bytes()!=before:return False
        os.replace(temporary,path);return True
    finally:
        if temporary is not None and temporary.exists():temporary.unlink()


def valid_feed_run(status,pid,session,now,run=None):
    """Bind only a fresh exact feed identity; malformed status is terminal."""
    if not isinstance(status,dict):return None
    token=status.get('run');tick=status.get('tick_ms')
    if (status.get('schema')!='srmm-feed-status-v1' or type(status.get('pid')) is not int or status['pid']!=pid or
        type(status.get('session')) is not int or status['session']!=session or
        not isinstance(token,str) or not re.fullmatch('[a-f0-9]{32}',token) or
        (run is not None and token!=run) or status.get('state')!='running' or
        type(tick) is not int or not 0<=now-tick<=2000):return None
    return token


def disable_owned_session(path,session,packet,attempts=20,pause=time.sleep):
    """Re-read under the shared lock so concurrent display edits cannot lose stop.

    Bound retries to 500 ms by default. Never disable a replacement session.
    A refused stop is reported by the caller; native packet freshness still gates.
    """
    for attempt in range(attempts):
        try:
            with config_lock(path):
                before,fields=read_fields(path)
                if int(fields['session'])!=session or fields['packet_path']!=str(packet):return False
                if fields.get('enabled')=='0':return True
                fields['enabled']='0'
                if _replace_locked(path,before,fields):return True
        except (PermissionError,BlockingIOError):pass
        if attempt+1<attempts:pause(.025)
    return False


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config',type=Path,required=True)
    parser.add_argument('--registry',type=Path,required=True)
    parser.add_argument('--pid',type=int,required=True)
    parser.add_argument('--seconds',type=int,default=600)
    parser.add_argument('--feed-status',type=Path)
    args=parser.parse_args()
    if not 1<=args.seconds<=28800 or not 0<args.pid<2**32:parser.error('Use a valid PID and 1..28800 seconds.')
    registry=load_registry(args.registry);_,initial=read_fields(args.config)
    session=int(initial['session']);packet=Path(initial['packet_path'])
    clock=ctypes.WinDLL('kernel32').GetTickCount64;clock.restype=ctypes.c_uint64
    deadline=time.monotonic()+args.seconds;last=object();run=None
    while time.monotonic()<deadline:
        before,fields=read_fields(args.config)
        if int(fields['session'])!=session or fields['packet_path']!=str(packet):raise ValueError('Session ownership changed; watcher stopped.')
        try:level=packet_map(packet.read_bytes(),args.pid,session,clock())
        except OSError:level=None
        terminal=False
        if args.feed_status:
            try:
                status=read_json(args.feed_status)
                accepted=valid_feed_run(status,args.pid,session,clock(),run)
                terminal=accepted is None
                if accepted is not None:run=accepted
            except (OSError,ValueError,KeyError,TypeError):terminal=True
            if terminal:
                if not disable_owned_session(args.config,session,packet):
                    raise RuntimeError('Feed ended but configuration could not be disabled within the bounded retry; native stale-packet rejection remains active.')
                print('Feed ended or changed; display disabled.',flush=True)
                break
        if level is None:
            time.sleep(.1);continue
        selected=choose(fields,registry,level)
        try:
            if replace_if_unchanged(args.config,before,selected) and level!=last:
                print('Selected registered map.' if level in registry else 'Map unavailable or unregistered; display disabled.',flush=True)
        except (PermissionError,BlockingIOError):pass
        last=level;time.sleep(.1)


if __name__=='__main__':main()
