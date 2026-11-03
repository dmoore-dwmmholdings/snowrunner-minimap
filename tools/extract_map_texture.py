"""Read the observed RGB map texture variant into a user-local DDS.

No game writes, texture downloads or asset redistribution. This is a bounded
decoder for observed PCT framing, not a general-purpose PCT implementation.
The output has no world calibration; that must be measured separately.
"""
import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import struct
import zipfile

MAX_BYTES = 96 * 1024 * 1024


@dataclass(frozen=True)
class MapTexture:
    width: int
    height: int
    rgb_blocks: bytes

    def dds(self):
        # DXT5/BC3 uses the observed 16-byte blocks' RGB color section. Its
        # alpha is deliberately ignored by the minimap; alpha is unvalidated.
        header = struct.pack('<7I', 124, 0x81007, self.height, self.width,
                             len(self.rgb_blocks), 0, 1)
        header += bytes(44)
        header += struct.pack('<8I', 32, 4, 0x35545844, 0, 0, 0, 0, 0)
        header += struct.pack('<5I', 0x1000, 0, 0, 0, 0)
        return b'DDS ' + header + self.rgb_blocks


def parse_map_texture(data):
    if not 98 <= len(data) <= MAX_BYTES:
        raise ValueError('PCT size outside supported bounds.')
    fields = {}
    position = 0
    while position < len(data):
        if len(fields) >= 16 or position + 6 > len(data):
            raise ValueError('Invalid field count or truncated field header.')
        key, end = struct.unpack_from('<HI', data, position)
        if key in fields or not position + 6 <= end <= len(data):
            raise ValueError('Duplicate field or invalid field boundary.')
        fields[key] = data[position + 6:end]
        position = end
    if set(fields) != {0xf0, 0x102, 0xf2, 0xf9, 0x107, 0x104, 0xff, 1}:
        raise ValueError('Unsupported field layout.')
    if fields[0xf0] != b'TCIP' or fields[1] or fields[0x104] != bytes(4):
        raise ValueError('Unsupported texture container.')
    if len(fields[0x102]) != 16 or len(fields[0xf2]) != 4 or len(fields[0xf9]) != 4:
        raise ValueError('Invalid texture metadata size.')
    width, height, depth, layers = struct.unpack('<4I', fields[0x102])
    if not (4 <= width <= 8192 and 4 <= height <= 8192 and
            width % 4 == height % 4 == 0 and depth == layers == 1):
        raise ValueError('Only bounded 2D block textures supported.')
    if struct.unpack('<I', fields[0xf2])[0] != 0x0f:
        raise ValueError('Unsupported map block format.')
    count = struct.unpack('<I', fields[0xf9])[0]
    if not 1 <= count <= 14 or len(fields[0x107]) != count * 8:
        raise ValueError('Invalid mip table.')
    payload = fields[0xff]
    cursor = 0
    for index in range(count):
        offset, size = struct.unpack_from('<II', fields[0x107], index * 8)
        expected = ((max(1, width >> index) + 3) // 4) * ((max(1, height >> index) + 3) // 4) * 16
        if offset != cursor or size != expected or offset + size > len(payload):
            raise ValueError('Invalid mip size or overlapping/out-of-bounds mip.')
        cursor += size
    if cursor != len(payload):
        raise ValueError('Trailing or missing texture bytes.')
    return MapTexture(width, height, payload[:width * height])


def read_map(pak, level):
    if not re.fullmatch(r'level_[a-z0-9_]{3,48}', level):
        raise ValueError('Invalid level identifier.')
    name = f'[textures]/pct/{level}_map.pct'
    with zipfile.ZipFile(pak) as archive:
        matches = [entry for entry in archive.infolist() if entry.filename == name]
        if len(matches) != 1 or not 98 <= matches[0].file_size <= MAX_BYTES:
            raise ValueError('Expected one bounded map texture entry.')
        with archive.open(matches[0]) as source:
            data = source.read(MAX_BYTES + 1)
        return parse_map_texture(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pak', type=Path)
    parser.add_argument('level')
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    try:
        source = args.pak.resolve(strict=True)
        output = args.output.resolve()
        # Installed layout is .../SnowRunner/preload/paks/client/*.pak.
        if source.parent.name.lower() == 'client' and output.is_relative_to(source.parents[3]):
            raise ValueError('Output must be outside the game installation.')
        texture = read_map(source, args.level)
        with output.open('xb') as destination:
            destination.write(texture.dds())
        print(f'Wrote local RGB map DDS: {texture.width}x{texture.height}; alpha and world calibration unverified.')
    except (ValueError, OSError, zipfile.BadZipFile) as error:
        parser.exit(1, f'Map import stopped: {error}\n')


if __name__ == '__main__':
    main()
