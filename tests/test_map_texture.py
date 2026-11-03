import importlib.util
from pathlib import Path
import struct
import sys
import io
import unittest
import zipfile

path = Path(__file__).resolve().parents[1] / 'tools' / 'extract_map_texture.py'
if not path.exists():
    path = Path(__file__).with_name('extract_map_texture.py')
spec = importlib.util.spec_from_file_location('map_texture', path)
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)


def fixture(**changes):
    # Original 4x4 red BC3 block, unrelated to game assets.
    block = b'\xff\xff' + bytes(6) + struct.pack('<HHI', 0xf800, 0xf800, 0)
    fields = [(0xf0, b'TCIP'), (0x102, struct.pack('<4I', 4, 4, 1, 1)),
              (0xf2, struct.pack('<I', 15)), (0xf9, struct.pack('<I', 1)),
              (0x107, struct.pack('<II', 0, 16)), (0x104, bytes(4)), (0xff, block), (1, b'')]
    out = b''
    for key, payload in fields:
        payload = changes.get(str(key), payload)
        out += struct.pack('<HI', key, len(out) + 6 + len(payload)) + payload
    return out


class MapTextureTests(unittest.TestCase):
    def test_valid_rgb_dds(self):
        texture = module.parse_map_texture(fixture())
        self.assertEqual((texture.width, texture.height), (4, 4))
        self.assertEqual(texture.dds()[:4], b'DDS ')
        self.assertEqual(texture.dds()[84:88], b'DXT5')
        self.assertEqual(len(texture.dds()), 144)

    def test_bad_field_bounds(self):
        for end in (0, 5, 0xffffffff):
            data = bytearray(fixture())
            struct.pack_into('<I', data, 2, end)
            with self.assertRaises(ValueError): module.parse_map_texture(data)

    def test_metadata_and_payload_rejected(self):
        for changes in ({str(0xf0): b'NOPE'}, {str(0xf2): struct.pack('<I', 51)},
                        {str(0x102): struct.pack('<4I', 8196, 4, 1, 1)},
                        {str(0x107): struct.pack('<II', 1, 16)},
                        {str(0xff): bytes(15)}, {str(0xff): bytes(17)},
                        {str(0xf9): struct.pack('<I', 2)}):
            with self.assertRaises(ValueError): module.parse_map_texture(fixture(**changes))

    def test_exact_archive_entry_and_safe_identifier(self):
        archive = io.BytesIO()
        with zipfile.ZipFile(archive, 'w') as z:
            z.writestr('[textures]/pct/level_test_map.pct', fixture())
        self.assertEqual(module.read_map(archive, 'level_test').width, 4)
        for name in ('../test', 'level_missing'):
            with self.assertRaises(ValueError): module.read_map(archive, name)


if __name__ == '__main__': unittest.main()
