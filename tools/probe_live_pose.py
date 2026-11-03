"""Bounded read-only SnowRunner candidate probe, not verified live telemetry.

No process writes, injection, debugger, code execution or protection changes.
Only the recorded executable hash is accepted. Layout candidates need independent
movement/heading/map validation before they can become a production adapter.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import time

EXPECTED_SHA256 = 'd46623b23808d19a40f7ebeec15332d11f97317637ec4d4e072a501bc78a77c8'
TRUCK_RTTI = b'.?AVTRUCK_CONTROL@combine@@'
# The first live inventory contained 19,265 unique pointer candidates. Keep a
# fixed ceiling above that observation; exceeding it still disables sampling.
MAX_POINTERS = 32768


def pointer(reader, address):
    raw = reader.read(address, 8)
    return struct.unpack('<Q', raw)[0] if raw else None


def truck_type(reader, base, image_size, address):
    vtable = pointer(reader, address)
    if not vtable or not base + 8 <= vtable < base + image_size:
        return False
    locator = pointer(reader, vtable - 8)
    if not locator or not base <= locator <= base + image_size - 24:
        return False
    raw = reader.read(locator, 24)
    if not raw:
        return False
    signature, _, _, type_rva, _, self_rva = struct.unpack('<6I', raw)
    if signature != 1 or base + self_rva != locator or type_rva > image_size - 96:
        return False
    name = reader.read(base + type_rva + 16, 80)
    return bool(name and name.split(b'\0', 1)[0] == TRUCK_RTTI)


def level_name(reader, address):
    raw = reader.read(address, 56)
    if not raw:
        return None
    length = struct.unpack_from('<I', raw, 16)[0]
    if not 6 <= length <= 31 or raw[24 + length] != 0:
        return None
    name = raw[24:24 + length]
    return name.decode('ascii') if re.fullmatch(rb'level_[a-z0-9_]+', name) else None


def read_pose(reader, root_slot):
    root = pointer(reader, root_slot)
    if not root:
        return None
    addresses = [root]
    node = root
    for offset in (0x08, 0x60, 0x68):
        node = pointer(reader, node + offset)
        if not node:
            return None
        addresses.append(node)
    raw = reader.read(node, 208)
    if not raw:
        return None
    orientation = struct.unpack_from('<3f', raw, 0xb0)
    position = struct.unpack_from('<3f', raw, 0xc0)
    if not all(math.isfinite(v) for v in position + orientation):
        return None
    if max(abs(v) for v in position) > 20000 or not .98 <= sum(v*v for v in orientation) <= 1.02:
        return None
    # Recheck the pointer chain. This reduces transition races, not a game lock.
    if pointer(reader, root_slot) != root:
        return None
    for address, offset, expected in zip(addresses, (0x08, 0x60, 0x68), addresses[1:]):
        if pointer(reader, address + offset) != expected:
            return None
    return {'positionCandidateXYZ': position, 'orientationCandidateXYZ': orientation}


def image_sections(blob):
    if blob[:2] != b'MZ' or len(blob) < 64:
        raise ValueError('Not a PE executable.')
    pe = struct.unpack_from('<I', blob, 60)[0]
    if pe + 264 > len(blob) or blob[pe:pe + 4] != b'PE\0\0':
        raise ValueError('Invalid PE header.')
    machine, count = struct.unpack_from('<HH', blob, pe + 4)
    optional_size = struct.unpack_from('<H', blob, pe + 20)[0]
    if machine != 0x8664 or optional_size < 64 or struct.unpack_from('<H', blob, pe + 24)[0] != 0x20b or count > 96:
        raise ValueError('Only bounded x64 PE32+ images supported.')
    size = struct.unpack_from('<I', blob, pe + 24 + 56)[0]
    table = pe + 24 + optional_size
    if table + count * 40 > len(blob):
        raise ValueError('Truncated section table.')
    sections = {}
    for index in range(count):
        start = table + index * 40
        name = blob[start:start + 8].split(b'\0')[0].decode('ascii')
        length, rva = struct.unpack_from('<II', blob, start + 8)
        if rva + length > size:
            raise ValueError('Section outside image.')
        sections[name] = (rva, length)
    if '.data' not in sections or not 0 < sections['.data'][1] <= 4 * 1024 * 1024:
        raise ValueError('Unsupported .data section size.')
    return size, sections


class MemoryInfo(c.Structure):
    _fields_ = [('base', c.c_void_p), ('allocation_base', c.c_void_p),
                ('allocation_protect', w.DWORD), ('partition', w.WORD),
                ('size', c.c_size_t), ('state', w.DWORD), ('protect', w.DWORD), ('type', w.DWORD)]


class WindowsReader:
    def __init__(self, pid):
        if c.sizeof(c.c_void_p) != 8 or c.sizeof(MemoryInfo) != 48:
            raise ValueError('64-bit Windows Python required.')
        self.kernel = c.WinDLL('kernel32', use_last_error=True)
        self.psapi = c.WinDLL('psapi', use_last_error=True)
        self.kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
        self.kernel.OpenProcess.restype = w.HANDLE
        self.kernel.CloseHandle.argtypes = [w.HANDLE]
        self.kernel.VirtualQueryEx.argtypes = [w.HANDLE, c.c_void_p, c.POINTER(MemoryInfo), c.c_size_t]
        self.kernel.VirtualQueryEx.restype = c.c_size_t
        self.kernel.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
        self.kernel.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD)]
        self.psapi.EnumProcessModulesEx.argtypes = [w.HANDLE, c.POINTER(w.HMODULE), w.DWORD, c.POINTER(w.DWORD), w.DWORD]
        self.psapi.GetModuleFileNameExW.argtypes = [w.HANDLE, w.HMODULE, w.LPWSTR, w.DWORD]
        self.handle = self.kernel.OpenProcess(0x0400 | 0x0010, False, pid)  # QUERY_INFORMATION | VM_READ only
        if not self.handle:
            raise OSError(c.get_last_error(), 'Read-only OpenProcess failed; no elevation attempted.')
        self.read_calls = 0

    def close(self):
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None

    def executable(self):
        buffer = c.create_unicode_buffer(32768)
        length = w.DWORD(len(buffer))
        if not self.kernel.QueryFullProcessImageNameW(self.handle, 0, buffer, c.byref(length)):
            raise OSError(c.get_last_error(), 'Cannot identify target executable.')
        return Path(buffer.value).resolve()

    def modules(self):
        modules = (w.HMODULE * 1024)()
        needed = w.DWORD()
        if not self.psapi.EnumProcessModulesEx(self.handle, modules, c.sizeof(modules), c.byref(needed), 3):
            raise OSError(c.get_last_error(), 'Module enumeration failed.')
        if needed.value > c.sizeof(modules):
            raise ValueError('Module count exceeds bound.')
        result = []
        for handle in modules[:needed.value // c.sizeof(w.HMODULE)]:
            buffer = c.create_unicode_buffer(32768)
            if self.psapi.GetModuleFileNameExW(self.handle, handle, buffer, len(buffer)):
                result.append((handle, Path(buffer.value).resolve()))
        return result

    def read(self, address, size):
        if not address or address < 65536 or size <= 0 or size > 4 * 1024 * 1024:
            return None
        info = MemoryInfo()
        if not self.kernel.VirtualQueryEx(self.handle, address, c.byref(info), c.sizeof(info)):
            return None
        if info.state != 0x1000 or info.protect & (0x100 | 0x01) or not info.protect & (0x02 | 0x04 | 0x08 | 0x20 | 0x40 | 0x80):
            return None  # Skip guard, inaccessible, uncommitted and execute-only pages.
        if address + size > info.base + info.size:
            return None
        buffer = c.create_string_buffer(size)
        received = c.c_size_t()
        self.read_calls += 1
        if not self.kernel.ReadProcessMemory(self.handle, address, buffer, size, c.byref(received)) or received.value != size:
            return None
        return buffer.raw


def discover(reader, base, image_size, data_section):
    rva, size = data_section
    candidates = []
    seen = set()
    unread_pages = 0
    for offset in range(0, size, 4096):
        raw = reader.read(base + rva + offset, min(4096, size - offset))
        if raw is None:
            unread_pages += 1
            continue
        for within in range(0, len(raw) - 7, 8):
            address = struct.unpack_from('<Q', raw, within)[0]
            if address % 8 == 0 and 65536 <= address < 0x800000000000 and address not in seen:
                seen.add(address)
                candidates.append((rva + offset + within, address))
    complete = len(candidates) <= MAX_POINTERS and unread_pages == 0
    trucks, levels = [], []
    for slot, address in candidates[:MAX_POINTERS]:
        if truck_type(reader, base, image_size, address):
            trucks.append({'rootRva': slot, 'object': address})
        name = level_name(reader, address)
        if name:
            levels.append({'rootRva': slot, 'object': address, 'name': name})
    return {'completeWithinBound': complete, 'pointerCandidates': len(candidates),
            'unreadDataPages': unread_pages, 'truckCandidates': trucks, 'levelCandidates': levels}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--samples', type=int, default=10)
    args = parser.parse_args()
    if not 1 <= args.samples <= 30:
        parser.error('--samples must be 1..30 (one per second).')
    reader = None
    try:
        reader = WindowsReader(args.pid)
        exe = reader.executable()
        if exe.name.lower() != 'snowrunner.exe':
            raise ValueError('Target is not SnowRunner.exe.')
        if args.output.resolve().is_relative_to(exe.parents[2]):
            raise ValueError('Reports must stay outside the game installation.')
        if args.output.exists():
            raise ValueError('Report destination must be new.')
        blob = exe.read_bytes()
        digest = hashlib.sha256(blob).hexdigest()
        if digest != EXPECTED_SHA256:
            raise ValueError('Executable hash differs from the inspected build; refusing candidate layout.')
        image_size, sections = image_sections(blob)
        modules = reader.modules()
        matches = [base for base, path in modules if path == exe]
        if len(matches) != 1:
            raise ValueError('Cannot identify exactly one executable module.')
        base = matches[0]
        result = {'schema': 'srmm-live-candidates-v1', 'status': 'UNVALIDATED; not production telemetry',
                  'pid': args.pid, 'executableSha256': digest, 'moduleBase': base,
                  'graphicsModulesPresent': [p.name for _, p in modules if p.name.lower() in ('d3d11.dll', 'd3d12.dll', 'dxgi.dll')],
                  'activeRenderer': 'unknown', 'discovery': discover(reader, base, image_size, sections['.data']), 'samples': []}
        discovery = result['discovery']
        if discovery['completeWithinBound'] and len(discovery['truckCandidates']) == len(discovery['levelCandidates']) == 1:
            truck = discovery['truckCandidates'][0]
            level = discovery['levelCandidates'][0]
            for index in range(args.samples):
                start = time.perf_counter()
                level_object = pointer(reader, base + level['rootRva'])
                name = level_name(reader, level_object) if level_object else None
                before_truck = pointer(reader, base + truck['rootRva'])
                pose = read_pose(reader, base + truck['rootRva'])
                current_truck = pointer(reader, base + truck['rootRva'])
                if not current_truck or current_truck != before_truck or not truck_type(reader, base, image_size, current_truck):
                    pose = None
                if (pointer(reader, base + level['rootRva']) != level_object or
                        (level_object and level_name(reader, level_object) != name)):
                    pose = None
                result['samples'].append({'monotonicSeconds': start, 'levelCandidate': name,
                                          'poseCandidate': pose if name else None})
                if index + 1 < args.samples:
                    time.sleep(1)
        result['readCalls'] = reader.read_calls
        with args.output.open('x', encoding='utf8') as stream:
            json.dump(result, stream, indent=2)
        print(json.dumps({'report': str(args.output), 'discovery': discovery,
                          'samples': len(result['samples']), 'readCalls': reader.read_calls}, indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, f'Probe stopped: {error}\n')
    finally:
        if reader:
            reader.close()


if __name__ == '__main__':
    main()
