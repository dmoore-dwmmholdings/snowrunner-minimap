"""Optional read-only UI-state publisher for an explicitly verified local profile.

No profile is bundled: metadata names and a single differential capture do not
establish game state. Unknown, unreadable or transitional data publishes UNKNOWN.
This tool never discovers offsets, calls game functions or changes game memory.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import tempfile
import time

from probe_live_pose import EXPECTED_SHA256, WindowsReader, image_sections, pointer
from feed_lifecycle import ProcessLifetime, MAX_SESSION_SECONDS

UNKNOWN, GAMEPLAY, MENU = 0, 1, 2


def encode_ui_state(pid, session, sequence, tick, state, build=EXPECTED_SHA256):
    if type(pid) is not int or not 0<pid<2**32 or type(state) is not int or state not in (UNKNOWN,GAMEPLAY,MENU):
        raise ValueError('Invalid process or UI state.')
    if any(type(v) is not int or not 0<v<2**64 for v in (session,sequence)) or type(tick) is not int or not 0<=tick<2**64:
        raise ValueError('Invalid UI-state identity/clock.')
    if not isinstance(build,str) or not re.fullmatch('[a-f0-9]{64}',build):raise ValueError('Invalid build fingerprint.')
    packet=bytearray(128)
    struct.pack_into('<8s4I3Q',packet,0,b'SRMMUI01',1,128,state,pid,sequence,tick,session)
    packet[48:80]=bytes.fromhex(build)
    return bytes(packet)


def load_profile(path, sections):
    if not 1<=path.stat().st_size<=65536:raise ValueError('UI profile exceeds bound.')
    data=json.loads(path.read_text(encoding='utf8'))
    if not isinstance(data,dict):raise ValueError('Expected a profile object.')
    if (data.get('schema')!='srmm-verified-ui-profile-v1' or data.get('verified') is not True or
            data.get('executableSha256')!=EXPECTED_SHA256):raise ValueError('An explicitly verified profile for this build is required.')
    validation=data.get('validation',{})
    if not isinstance(validation,dict) or not isinstance(validation.get('observed_states'),list) or not all(isinstance(v,str) for v in validation['observed_states']):raise ValueError('Invalid verification history.')
    required={'gameplay','pause','settings','full_map','gameplay_return'}
    if (type(validation.get('gameplay_returns')) is not int or validation['gameplay_returns']<2 or
            not required.issubset(set(validation.get('observed_states',[])))):
        raise ValueError('Profile must record repeated labeled gameplay/menu verification.')
    start,length=sections['.data']
    def bounded(rva,width):
        return type(rva) is int and start<=rva<=start+length-width
    predicates=data.get('predicates',[]);anchors=data.get('anchors',[])
    if not isinstance(predicates,list) or not 1<=len(predicates)<=16 or not isinstance(anchors,list) or not 1<=len(anchors)<=4:
        raise ValueError('Expected 1..16 predicates and 1..4 verified RTTI anchors.')
    seen=set()
    for p in predicates:
        if not isinstance(p,dict):raise ValueError('Invalid predicate object.')
        width=p.get('width');rva=p.get('rva')
        if type(width) is not int or width not in (1,2,4) or not bounded(rva,width) or (rva,width) in seen:raise ValueError('Predicate outside bounded data section or duplicate.')
        seen.add((rva,width))
        for key in ('gameplay','menu'):
            v=p.get(key)
            if not isinstance(v,list) or not 1<=len(v)<=16 or any(type(x) is not int or not 0<=x<2**(8*width) for x in v):raise ValueError('Invalid predicate values.')
        if set(p['gameplay']) & set(p['menu']):raise ValueError('Ambiguous gameplay/menu values.')
    for a in anchors:
        if not isinstance(a,dict):raise ValueError('Invalid anchor object.')
        rva=a.get('rootRva');name=a.get('type')
        if not bounded(rva,8) or rva%8 or not isinstance(name,str) or not 1<=len(name)<=200 or not re.fullmatch(r'[ -~]+',name):raise ValueError('Invalid typed root anchor.')
    return data


def rtti_name(reader,base,size,address):
    vt=pointer(reader,address)
    if not vt or not base+8<=vt<base+size:return None
    locator=pointer(reader,vt-8)
    if not locator or not base<=locator<=base+size-24:return None
    raw=reader.read(locator,24)
    if not raw:return None
    signature,_,_,type_rva,_,self_rva=struct.unpack('<6I',raw)
    if signature!=1 or base+self_rva!=locator or type_rva>size-256:return None
    raw=reader.read(base+type_rva+16,240)
    if not raw or b'\0' not in raw:return None
    try:return raw.split(b'\0')[0].decode('ascii')
    except UnicodeDecodeError:return None


def classify_values(profile, values):
    if len(values)!=len(profile['predicates']) or any(v is None for v in values):return UNKNOWN
    gameplay=all(v in p['gameplay'] for p,v in zip(profile['predicates'],values))
    menu=all(v in p['menu'] for p,v in zip(profile['predicates'],values))
    return GAMEPLAY if gameplay and not menu else MENU if menu and not gameplay else UNKNOWN


def sample_ui(reader,base,size,profile):
    roots=[]
    for anchor in profile['anchors']:
        root=pointer(reader,base+anchor['rootRva'])
        if not root or rtti_name(reader,base,size,root)!=anchor['type']:return UNKNOWN
        roots.append(root)
    def values():
        result=[]
        for p in profile['predicates']:
            raw=reader.read(base+p['rva'],p['width'])
            result.append(int.from_bytes(raw,'little') if raw is not None else None)
        return result
    before=values();after=values()
    if before!=after:return UNKNOWN
    for anchor,root in zip(profile['anchors'],roots):
        if pointer(reader,base+anchor['rootRva'])!=root or rtti_name(reader,base,size,root)!=anchor['type']:return UNKNOWN
    return classify_values(profile,before)


def publish(path, payload):
    temporary=None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent,prefix='ui-next-',suffix='.local.tmp',delete=False) as out:
            temporary=Path(out.name);out.write(payload)
        for attempt in range(6):
            try:os.replace(temporary,path);return
            except OSError as error:
                if getattr(error,'winerror',None) not in (5,32) or attempt==5:raise
                time.sleep(.02)
    finally:
        if temporary is not None and temporary.exists():temporary.unlink()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile',type=Path,required=True)
    parser.add_argument('--packet',type=Path,required=True)
    parser.add_argument('--pid',type=int,required=True)
    parser.add_argument('--session',type=int,required=True)
    parser.add_argument('--seconds',type=int,default=600)
    args=parser.parse_args()
    if not 0<args.pid<2**32 or not 0<args.session<2**64 or not 1<=args.seconds<=MAX_SESSION_SECONDS:parser.error('Invalid explicit process/session/duration.')
    reader=None;lock=None;admitted=False;sequence=0
    try:
        reader=WindowsReader(args.pid);exe=reader.executable();blob=exe.read_bytes()
        if exe.name.lower()!='snowrunner.exe' or hashlib.sha256(blob).hexdigest()!=EXPECTED_SHA256:raise ValueError('Unsupported game fingerprint.')
        size,sections=image_sections(blob);profile=load_profile(args.profile,sections)
        bases=[base for base,path in reader.modules() if path==exe]
        if len(bases)!=1:raise ValueError('Ambiguous executable module.')
        packet=args.packet.resolve()
        if not packet.parent.is_dir() or packet.is_relative_to(exe.parents[2]) or packet==args.profile.resolve():raise ValueError('Use an existing private directory outside the game.')
        import msvcrt
        lock=packet.with_name(packet.name+'.lock').open('a+b');lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1)
        if packet.exists():
            old=packet.read_bytes()
            if (len(old)!=128 or old[:8]!=b'SRMMUI01' or struct.unpack_from('<2I',old,8)!=(1,128) or
                struct.unpack_from('<I',old,20)[0]!=args.pid or struct.unpack_from('<Q',old,40)[0]!=args.session or
                old[48:80]!=bytes.fromhex(EXPECTED_SHA256) or any(old[80:])):raise ValueError('Existing packet belongs to another or invalid context.')
            sequence=struct.unpack_from('<Q',old,24)[0]
            if not 0<sequence<2**64-1:raise ValueError('Invalid prior sequence.')
        clock=ctypes.WinDLL('kernel32').GetTickCount64;clock.restype=ctypes.c_uint64
        admitted=True;deadline=time.monotonic()+args.seconds;lifetime=ProcessLifetime(reader.handle)
        try:
            while time.monotonic()<deadline and lifetime.alive():
                start=time.monotonic();tick=clock();state=sample_ui(reader,bases[0],size,profile);sequence+=1
                publish(packet,encode_ui_state(args.pid,args.session,sequence,tick,state))
                time.sleep(max(0,.05-(time.monotonic()-start)))
        finally:
            sequence+=1;publish(packet,encode_ui_state(args.pid,args.session,sequence,clock(),UNKNOWN))
        print('UI-state publisher stopped; final state UNKNOWN.')
    except (OSError,ValueError,KeyError,TypeError) as error:parser.exit(1,f'UI-state publisher refused/stopped: {error}\n')
    finally:
        if reader:reader.close()
        if lock:lock.close()


if __name__=='__main__':main()
