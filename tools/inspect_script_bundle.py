"""Inventory the observed S3DB/SSLB container without executing game scripts.

Only the framing of the observed bundle variant is recognized. Identifier hits
are byte-pattern candidates, not decoded instructions, signatures or call edges.
"""
import argparse
import hashlib
import json
import re
import struct
import zipfile
from pathlib import Path

PREFIX = bytes.fromhex('5333444201000200010001')
TABLE_PREFIX = bytes.fromhex('0200000001')
MODULE_PREFIX = bytes.fromhex('53534c4202000200')
MAX_BUNDLE_BYTES = 32 * 1024 * 1024
MAX_MODULES = 8192
DEFAULT_MODULES = (
    'game.game_states.game_state_minimap.minimap_info_provider',
    'game.minimap',
    'game.truck_manager.trucks_manager',
    'ui.components.minimap.minimap_camera_controller',
    'ui.components.minimap.minimap_compass_controller',
    'ui.components.minimap.objects.minimap_truck_controller',
)
IDENTIFIERS = frozenset((
    'GetCurrentTruck', 'GetPosition', 'GetDirection', 'GetRotation',
    'GetTransform', 'GetLevelName', 'GetCurrentGameSession', 'GetTrucksManager',
    'ConvertWorldPositionToMinimap', 'ConvertMinimapPositionToUI',
    'ConvertUIPositionToMinimap', 'OnCurrentTruckChanged', 'HandleOnLevelLoaded',
    'HandleOnTermLevel', 'GetActiveCamera', 'SetMinimap', 'Minimap',
    'MinimapPointInfo', 'UiMinimapTruckdirectionindicator', 'directionIndicator',
    'rotation', 'curTruck', 'levelData', 'minimapPos', 'mapMaxX', 'mapMaxZ',
))


def parse_bundle(blob):
    """Return validated module framing; all offsets refer to bundle bytes."""
    if len(blob) > MAX_BUNDLE_BYTES or not blob.startswith(PREFIX):
        raise ValueError('Unsupported S3DB header or bundle size.')
    position = len(PREFIX)

    def take(size):
        nonlocal position
        if size < 0 or position + size > len(blob):
            raise ValueError('Truncated bundle.')
        result = blob[position:position + size]
        position += size
        return result

    def uint32():
        return struct.unpack('<I', take(4))[0]

    count = uint32()
    if not 1 <= count <= MAX_MODULES:
        raise ValueError('Module count exceeds supported bounds.')
    if take(len(TABLE_PREFIX)) != TABLE_PREFIX:
        raise ValueError('Unsupported name-table header.')
    names = []
    for _ in range(count):
        length = uint32()
        if not 1 <= length <= 512:
            raise ValueError('Invalid module name length.')
        raw = take(length)
        if any(c < 32 or c > 126 for c in raw):
            raise ValueError('Unsupported module name encoding.')
        names.append(raw.decode('ascii'))
    if len(set(names)) != len(names):
        raise ValueError('Duplicate module names.')
    if take(1) != b'\x01':
        raise ValueError('Unsupported size-table marker.')
    sizes = [uint32() for _ in range(count)]
    if any(s < 16 for s in sizes) or sum(sizes) != len(blob) - position:
        raise ValueError('Module sizes do not cover the bundle exactly.')
    modules = []
    for index, (name, size) in enumerate(zip(names, sizes)):
        start = position
        chunk = take(size)
        if not chunk.startswith(MODULE_PREFIX):
            raise ValueError('Unsupported SSLB module header.')
        modules.append({'index': index, 'name': name, 'offset': start,
                        'bytes': size, 'sha256': hashlib.sha256(chunk).hexdigest()})
    return modules


def identifier_candidates(chunk, bundle_offset):
    hits = []
    # Observed short identifier encoding only. No bytecode grammar is assumed.
    for match in re.finditer(rb'\x32([\x05-\x7f])', chunk):
        length = match.group(1)[0]
        raw = chunk[match.end():match.end() + length]
        if len(raw) != length:
            continue
        name = raw.decode('ascii', errors='replace')
        if name in IDENTIFIERS:
            hits.append({'name': name, 'bundleOffset': bundle_offset + match.end()})
    return hits


def inspect_archive(path, member, selected):
    with zipfile.ZipFile(path) as archive:
        entries = [i for i in archive.infolist() if i.filename == member]
        if len(entries) != 1:
            raise ValueError('Expected exactly one matching archive member.')
        entry = entries[0]
        if entry.flag_bits & 1 or entry.file_size > MAX_BUNDLE_BYTES:
            raise ValueError('Encrypted or oversized bundle is unsupported.')
        with archive.open(entry) as stream:
            blob = stream.read(MAX_BUNDLE_BYTES + 1)
        if len(blob) != entry.file_size:
            raise ValueError('Bundle size changed or exceeded its limit.')
    modules = parse_bundle(blob)
    by_name = {m['name']: m for m in modules}
    missing = sorted(set(selected) - by_name.keys())
    if missing:
        raise ValueError('Requested modules absent: ' + ', '.join(missing))
    result = []
    for name in selected:
        module = by_name[name]
        start, size = module['offset'], module['bytes']
        result.append({**module, 'identifierCandidates':
                       identifier_candidates(blob[start:start + size], start)})
    return {
        'schema': 'snowrunner-script-inventory-v1', 'member': member,
        'bundleBytes': len(blob), 'bundleSha256': hashlib.sha256(blob).hexdigest(),
        'moduleCount': len(modules), 'modules': result,
        'limitations': [
            'Only the observed S3DB/SSLB framing variant is accepted.',
            'Identifier hits are byte-pattern candidates, not decoded call edges or signatures.',
            'Offsets refer to uncompressed bundle bytes, never process-memory addresses.',
            'No game execution, runtime access, extraction or installation changes.',
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--member', default='[ssl_cache]/initial_release.sslbundle')
    parser.add_argument('--module', action='append', dest='modules')
    parser.add_argument('--output', type=Path, help='Report destination outside the game installation.')
    args = parser.parse_args()
    try:
        if args.output and args.output.resolve() == args.archive.resolve():
            raise ValueError('Report must not overwrite the input archive.')
        report = inspect_archive(args.archive, args.member, args.modules or DEFAULT_MODULES)
        text = json.dumps(report, indent=2) + '\n'
        if args.output:
            # Refuse existing destinations so a report cannot overwrite game files.
            with args.output.open('x', encoding='utf-8') as output:
                output.write(text)
        else:
            print(text, end='')
    except (OSError, ValueError, zipfile.BadZipFile, RuntimeError) as error:
        parser.exit(1, f'Inspection failed: {error}\n')


if __name__ == '__main__':
    main()
