"""Explicit, bounded diagnostic candidate feed. Never auto-attaches or scans.

The caller supplies a prior discovery report and an explicit PID. This does not
establish gameplay/menu state or validate the candidate layout/calibration.
"""
import argparse
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import re
import secrets
import struct
import time

from probe_live_pose import (EXPECTED_SHA256, WindowsReader, image_sections,
                             pointer, truck_type, level_name, read_pose)


def encode_packet(pid, session, sequence, tick_ms, build, level, pose):
    if (type(pid) is not int or not 0 < pid < 2**32 or
            any(type(v) is not int or not 0 < v < 2**64 for v in (session, sequence)) or
            type(tick_ms) is not int or not 0 <= tick_ms < 2**64):
        raise ValueError('Invalid identity, sequence or timestamp.')
    if not re.fullmatch(r'[a-f0-9]{64}', build) or not re.fullmatch(r'level_[a-z0-9_]{3,48}', level):
        raise ValueError('Invalid build or map identifier.')
    values = (0.,)*6
    if pose is not None:
        values = tuple(pose['positionCandidateXYZ']) + tuple(pose['orientationCandidateXYZ'])
        if len(values) != 6 or not all(math.isfinite(v) for v in values):
            raise ValueError('Invalid candidate coordinates.')
        if max(abs(v) for v in values[:3]) > 20000 or not .98 <= sum(v*v for v in values[3:]) <= 1.02 or math.hypot(values[3], values[5]) < .01:
            raise ValueError('Invalid candidate direction or extent.')
    packet = bytearray(256)
    struct.pack_into('<8s4I3Q', packet, 0, b'SRMMTEL1', 1, 256, int(pose is not None), pid, sequence, tick_ms, session)
    encoded = level.encode('ascii');packet[48:48+len(encoded)] = encoded
    packet[112:144] = bytes.fromhex(build)
    struct.pack_into('<6d', packet, 144, *values)
    return bytes(packet)


def replace_packet(temporary, destination, replace=os.replace, pause=time.sleep):
    # Windows readers/security scanners can briefly retain a rename-sensitive handle.
    # Retry only ordinary sharing/access conflicts; never alter their protection.
    for attempt in range(11):
        try:
            replace(temporary, destination)
            return
        except OSError as error:
            if getattr(error, 'winerror', None) not in (5, 32) or attempt == 10:
                raise
            pause(.025)


def resume_sequence(data, pid, session, level):
    if len(data) != 256 or data[:8] != b'SRMMTEL1' or struct.unpack_from('<2I',data,8) != (1,256):
        raise ValueError('Invalid prior diagnostic packet.')
    if (struct.unpack_from('<I',data,20)[0] != pid or struct.unpack_from('<Q',data,40)[0] != session or
            data[48:112] != level.encode('ascii').ljust(64,b'\0') or data[112:144] != bytes.fromhex(EXPECTED_SHA256)):
        raise ValueError('Prior packet belongs to another context.')
    sequence = struct.unpack_from('<Q',data,24)[0]
    if not 0 < sequence < 2**64-1:raise ValueError('Invalid prior sequence.')
    return sequence


def select_roots(report, sections):
    if report.get('schema') != 'srmm-live-candidates-v1' or report.get('executableSha256') != EXPECTED_SHA256:
        raise ValueError('Wrong discovery report schema/build.')
    discovery = report.get('discovery', {})
    if not discovery.get('completeWithinBound'):
        raise ValueError('Incomplete prior discovery.')
    start, length = sections['.data']
    roots = []
    for key in ('truckCandidates', 'levelCandidates'):
        entries = discovery.get(key, [])
        if len(entries) != 1:
            raise ValueError('Discovery must contain exactly one candidate per role.')
        rva = entries[0].get('rootRva')
        if type(rva) is not int or rva % 8 or not start <= rva <= start + length - 8:
            raise ValueError('Root slot outside the bounded data section.')
        roots.append(rva)
    if roots[0] == roots[1]:
        raise ValueError('Distinct root roles required.')
    return tuple(roots)


def correct_truck_axis(pose):
    # The inspected pose basis is lateral. Convert it to forward in the X/Z plane.
    # Keep encode_packet generic: only this game-specific adapter corrects the basis.
    result = dict(pose)
    x, _, z = pose['orientationCandidateXYZ']
    n = math.hypot(x, z)
    if n < .01: raise ValueError('Degenerate truck heading.')
    result['orientationCandidateXYZ'] = [z/n, 0., -x/n]
    return result


def sample_fixed(reader, base, image_size, roots):
    truck_slot, level_slot = (base + rva for rva in roots)
    truck = pointer(reader, truck_slot);level = pointer(reader, level_slot)
    if not truck or not level or not truck_type(reader, base, image_size, truck):
        return None, None
    name = level_name(reader, level)
    pose = read_pose(reader, truck_slot) if name else None
    if (pointer(reader, truck_slot) != truck or pointer(reader, level_slot) != level or
            level_name(reader, level) != name or not truck_type(reader, base, image_size, truck)):
        return None, None
    return name, correct_truck_axis(pose) if pose else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--layout', type=Path, required=True)
    parser.add_argument('--map', required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=30)
    parser.add_argument('--resume', action='store_true', help='Resume the exact explicit private session after a stopped publisher.')
    parser.add_argument('--session', type=int, help='Explicit nonzero diagnostic session identity prepared before game startup.')
    args = parser.parse_args()
    if not 1 <= args.seconds <= 600 or not re.fullmatch(r'level_[a-z0-9_]{3,48}', args.map):
        parser.error('Duration must be 1..600 seconds and map must be a canonical level identifier.')
    if args.session is not None and not 0 < args.session < 2**64:parser.error('Session must be a nonzero uint64.')
    reader = None;directory = None;lock = None;sequence = 0;session = args.session or secrets.randbits(64) or 1
    try:
        if args.layout.stat().st_size > 1024*1024:
            raise ValueError('Discovery report exceeds size limit.')
        report = json.loads(args.layout.read_text(encoding='utf8'))
        reader = WindowsReader(args.pid);exe = reader.executable()
        if exe.name.lower() != 'snowrunner.exe':raise ValueError('Target is not SnowRunner.')
        blob = exe.read_bytes()
        if hashlib.sha256(blob).hexdigest() != EXPECTED_SHA256:raise ValueError('Unsupported executable fingerprint.')
        size, sections = image_sections(blob);roots = select_roots(report, sections)
        bases = [base for base, path in reader.modules() if path == exe]
        if len(bases) != 1:raise ValueError('Ambiguous executable module.')
        output = args.output_dir.resolve()
        if output.is_relative_to(exe.parents[2]):raise ValueError('Output must remain outside the game installation.')
        if args.resume and args.session is None:raise ValueError('Resume requires an explicit session.')
        if not args.resume:output.mkdir(exist_ok=False)
        elif not output.is_dir():raise ValueError('Resume directory missing.')
        import msvcrt
        lock = (output/'publisher.local.lock').open('a+b')
        lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1)
        if args.resume:
            identity = json.loads((output/'session.local.json').read_text())
            if any(identity.get(k) != v for k,v in {'pid':args.pid,'session':session,'map':args.map,'build':EXPECTED_SHA256}.items()):
                raise ValueError('Resume identity does not match.')
            sequence = resume_sequence((output/'candidate.local.bin').read_bytes(),args.pid,session,args.map)
        directory = output
        clock = ctypes.WinDLL('kernel32').GetTickCount64;clock.restype = ctypes.c_uint64
        (directory/'session.local.json').write_text(json.dumps({'schema':'srmm-diagnostic-feed-v1','pid':args.pid,'session':session,'map':args.map,'build':EXPECTED_SHA256,'clock':'GetTickCount64','status':'unvalidated candidate; no gameplay-state signal'}))
        def write(level, pose, sampled_tick=None):
            nonlocal sequence
            sequence += 1
            payload = encode_packet(args.pid,session,sequence,clock() if sampled_tick is None else sampled_tick,EXPECTED_SHA256,level or args.map,pose)
            temporary = directory/'next.local.tmp';temporary.write_bytes(payload)
            replace_packet(temporary,directory/'candidate.local.bin')
        deadline = time.monotonic() + args.seconds
        try:
            while time.monotonic() < deadline:
                tick = time.monotonic();sampled_tick = clock();name,pose = sample_fixed(reader,bases[0],size,roots)
                write(name,pose,sampled_tick)
                time.sleep(max(0,.05-(time.monotonic()-tick)))
        finally:
            write(args.map,None)  # A stopped publisher never leaves an active packet behind.
        print('Diagnostic feed stopped; final packet unavailable. No gameplay/calibration validation is implied.')
    except (OSError,ValueError,KeyError,TypeError) as error:
        parser.exit(1,f'Candidate feed stopped: {error}\n')
    finally:
        if reader:reader.close()
        if lock:lock.close()


if __name__ == '__main__':main()
