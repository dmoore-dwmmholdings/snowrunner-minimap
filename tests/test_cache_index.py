import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from inspect_cache_index import parse_index


def fixture():
    names = [b'<ps>:example.sso', b'<ps>:another.sso']
    payloads = [b'original synthetic object', b'another synthetic object']
    blob = (b'1SERcache_block'.ljust(36, b'\0')
            + b'S3DRESOURCE'.ljust(16, b' ') + bytes(12)
            + struct.pack('<IBII', 1, 1, len(names), 4) + b'\1')
    for name in names:
        blob += struct.pack('<I', len(name)) + name
    offset_table = len(blob) + 1
    blob += b'\1' + struct.pack('<2Q', 0, len(payloads[0]))
    blob += b'\1' + struct.pack('<2I', *map(len, payloads))
    blob += b'\1' + bytes(8)
    return blob + b''.join(payloads), offset_table, payloads


class CacheIndexTests(unittest.TestCase):
    def test_exact_payload_boundaries(self):
        blob, _, payloads = fixture()
        resources = parse_index(blob)
        self.assertEqual(len(resources), 2)
        self.assertEqual([blob[r.offset:r.offset + r.size] for r in resources], payloads)

    def test_every_truncation_rejected(self):
        blob, _, _ = fixture()
        for end in range(len(blob)):
            with self.subTest(end=end), self.assertRaises(ValueError):
                parse_index(blob[:end])

    def test_gap_and_overlap_rejected(self):
        blob, offsets, payloads = fixture()
        for delta in [-1, 1]:
            changed = bytearray(blob)
            struct.pack_into('<Q', changed, offsets + 8, len(payloads[0]) + delta)
            with self.assertRaises(ValueError):
                parse_index(changed)

    def test_unknown_header_and_trailing_bytes_rejected(self):
        blob, _, _ = fixture()
        for changed in [b'BAD!' + blob[4:], blob + b'x', blob[:77] + b'\2' + blob[78:]]:
            with self.assertRaises(ValueError):
                parse_index(changed)

    def test_huge_count_and_name_rejected(self):
        blob, _, _ = fixture()
        for offset in [69, 78]:
            changed = bytearray(blob)
            struct.pack_into('<I', changed, offset, 0xffffffff)
            with self.assertRaises(ValueError):
                parse_index(changed)


if __name__ == '__main__':
    unittest.main()
