"""Strict read-only index inspector for the observed SnowRunner cache container.

This inferred format is not a writer, loader, or ABI. Unknown variants fail
closed. Reports contain names/ranges only, never serialized game payloads.
"""
from dataclasses import dataclass, asdict
import argparse
import json
import struct
import zipfile


@dataclass(frozen=True)
class Resource:
    name: str
    offset: int
    size: int
    opaque_flags: int


def parse_index(blob: bytes) -> list[Resource]:
    if len(blob) < 78:
        raise ValueError('Truncated cache header')
    if (blob[:36] != b'1SERcache_block'.ljust(36, b'\0')
            or blob[36:52] != b'S3DRESOURCE'.ljust(16, b' ')
            or blob[52:64] != bytes(12)
            or blob[64:69] != struct.pack('<IB', 1, 1)):
        raise ValueError('Unsupported cache header')
    count, width = struct.unpack_from('<II', blob, 69)
    if not 1 <= count <= 100_000 or width != 4:
        raise ValueError('Unsupported resource count/table variant')
    pos = 77

    def take(n):
        nonlocal pos
        if n > len(blob) - pos:
            raise ValueError('Truncated cache table')
        result = blob[pos:pos + n]
        pos += n
        return result

    def marker():
        if take(1) != b'\1':
            raise ValueError('Unsupported cache table marker')

    marker()
    names = []
    for _ in range(count):
        length = struct.unpack('<I', take(4))[0]
        if not 1 <= length <= 4096:
            raise ValueError('Invalid resource name length')
        raw = take(length)
        if any(c < 32 or c > 126 for c in raw):
            raise ValueError('Unsupported resource name encoding')
        names.append(raw.decode('ascii'))
    if len(set(names)) != count:
        raise ValueError('Duplicate resource names')
    marker()
    offsets = struct.unpack(f'<{count}Q', take(8 * count))
    marker()
    sizes = struct.unpack(f'<{count}I', take(4 * count))
    marker()
    flags = struct.unpack(f'<{count}I', take(4 * count))
    if any(flags):
        raise ValueError('Unsupported opaque resource flags')
    expected = 0
    for offset, size in zip(offsets, sizes):
        if offset != expected:
            raise ValueError('Non-contiguous resource ranges')
        expected += size
    if expected != len(blob) - pos:
        raise ValueError('Resource ranges do not cover payload exactly')
    return [Resource(name, pos + offset, size, flag)
            for name, offset, size, flag in zip(names, offsets, sizes, flags)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive')
    parser.add_argument('--member', default='initial.cache_block')
    parser.add_argument('--name', action='append', help='Exact resource name to report')
    args = parser.parse_args()
    try:
        with zipfile.ZipFile(args.archive) as archive:
            member = archive.getinfo(args.member)
            if member.file_size > 128 * 1024 * 1024:
                raise ValueError('Cache exceeds inspection bound')
            entries = parse_index(archive.read(member))
        selected = set(args.name or [])
        if selected - {e.name for e in entries}:
            raise ValueError('Requested resource absent')
        print(json.dumps({'schema': 'inferred-cache-index-v1', 'count': len(entries),
                          'resources': [asdict(e) for e in entries
                                        if not selected or e.name in selected]}, indent=2))
    except (OSError, ValueError, zipfile.BadZipFile, KeyError) as error:
        parser.exit(1, f'Inspection failed: {error}\n')


if __name__ == '__main__':
    main()
