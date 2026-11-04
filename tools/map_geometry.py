"""Bounded terrain geometry reader for the observed local SnowRunner layout.

Reads actual width, depth and world origin from scene metadata. Terrain block
counts and the independent height texture header must agree. This does not mark
visual alignment in the running game as verified.
"""
from dataclasses import dataclass
import hashlib
import math
from pathlib import Path
import re
import struct
import zipfile

MAX_SCENE_BYTES = 64 * 1024 * 1024
PREFIX_BYTES = 65536
BLOCK_METERS = 24
CELLS_PER_BLOCK = 48


@dataclass(frozen=True)
class TerrainBounds:
    width: float
    depth: float
    origin_x: float
    origin_z: float
    blocks_x: int
    blocks_z: int
    offset: int

    @property
    def affine(self):
        # North-up UV, with north corresponding to increasing world Z.
        return [1/self.width, 0., -self.origin_x/self.width,
                0., -1/self.depth, 1+self.origin_z/self.depth]


def height_header_dimensions(data):
    """Accept the small, one-mip R16 terrain-header variant only."""
    if len(data) != 82:
        raise ValueError('Unsupported terrain header size.')
    fields = {}
    offset = 0
    while offset < len(data):
        if offset+6 > len(data): raise ValueError('Truncated terrain header.')
        key,end = struct.unpack_from('<HI',data,offset)
        if key in fields or not offset+6 <= end <= len(data):
            raise ValueError('Invalid terrain header framing.')
        fields[key] = data[offset+6:end]
        offset = end
    if (set(fields) != {0xf0,0x102,0xf2,0xf9,0x107,0x104,1} or
            fields[0xf0] != b'TCIP' or fields[1] or fields[0x104] != bytes(4) or
            len(fields[0x102]) != 16 or fields[0xf2] != struct.pack('<I',0x37) or
            fields[0xf9] != struct.pack('<I',1) or len(fields[0x107]) != 8):
        raise ValueError('Unsupported terrain header layout.')
    width,depth,layers,faces = struct.unpack('<4I',fields[0x102])
    start,size = struct.unpack('<2I',fields[0x107])
    if (layers != 1 or faces != 1 or not 49 <= width <= 8193 or
            not 49 <= depth <= 8193 or (width-1)%48 or (depth-1)%48 or
            start != 0 or size != width*depth*2):
        raise ValueError('Invalid terrain dimensions or payload declaration.')
    return width,depth


def find_terrain_bounds(prefix, dimensions):
    """Find one self-consistent <4f2I> terrain record in a bounded scene prefix.

    Requiring independent node counts prevents accepting arbitrary plausible
    floats. Ambiguity disables import. Do not weaken these checks for new builds.
    """
    if not 40 <= len(prefix) <= PREFIX_BYTES:
        raise ValueError('Invalid bounded scene prefix.')
    if len(dimensions) != 2 or any(type(v) is not int or v < 49 or v > 8193 or (v-1)%48 for v in dimensions):
        raise ValueError('Invalid terrain grid dimensions.')
    expected_blocks = tuple((v-1)//CELLS_PER_BLOCK for v in dimensions)
    matches = []
    for offset in range(16,len(prefix)-23):
        width,depth,x,z,u,v = struct.unpack_from('<4f2I',prefix,offset)
        if (u,v) != expected_blocks or width != u*BLOCK_METERS or depth != v*BLOCK_METERS:
            continue
        if not all(math.isfinite(value) for value in (width,depth,x,z)):
            continue
        if max(abs(x),abs(z),abs(x+width),abs(z+depth)) > 20000:
            continue
        matches.append(TerrainBounds(width,depth,x,z,u,v,offset))
    if len(matches) != 1:
        raise ValueError(f'Expected one independently matched terrain record; found {len(matches)}.')
    return matches[0]


def inspect_geometry(level_pak, level):
    if not re.fullmatch(r'level_[a-z0-9_]{3,48}',level):
        raise ValueError('Invalid map identifier.')
    if Path(level_pak).stem != level:
        raise ValueError('Level archive identity mismatch.')
    height_member = f'[textures]/pct/{level}_height_map.pct_header'
    scene_member = '[scene]/data_for_ssl.bin'
    with zipfile.ZipFile(level_pak) as archive:
        entries=archive.infolist()
        if len(entries)>200000:raise ValueError('Archive inventory exceeds bound.')
        def unique(name,minimum,maximum):
            found=[entry for entry in entries if entry.filename==name]
            if len(found)!=1 or not minimum<=found[0].file_size<=maximum:
                raise ValueError('Missing, duplicate or oversized geometry member.')
            return found[0]
        hi=unique(height_member,82,82)
        si=unique(scene_member,40,MAX_SCENE_BYTES)
        header=archive.read(hi)
        with archive.open(si) as source:prefix=source.read(PREFIX_BYTES)
        dimensions=height_header_dimensions(header)
        bounds=find_terrain_bounds(prefix,dimensions)
        evidence={
            'schema':'srmm-terrain-geometry-evidence-v1','level':level,
            'archive':str(Path(level_pak).resolve()),'scene_member':scene_member,
            'scene_size':si.file_size,'scene_crc32':f'{si.CRC:08x}',
            'prefix_sha256':hashlib.sha256(prefix).hexdigest(),
            'height_header_sha256':hashlib.sha256(header).hexdigest(),
            'height_nodes':list(dimensions),'record_offset':bounds.offset,
            'world_extent':[bounds.width,bounds.depth],
            'world_origin':[bounds.origin_x,bounds.origin_z],
            'terrain_blocks':[bounds.blocks_x,bounds.blocks_z],
            'runtime_alignment_verified':False}
        return bounds,evidence


def check_map_dimensions(bounds, map_dimensions):
    expected=(bounds.blocks_x*CELLS_PER_BLOCK,bounds.blocks_z*CELLS_PER_BLOCK)
    if tuple(map_dimensions)!=expected:
        raise ValueError('Map image dimensions do not match the observed full-terrain image layout.')

