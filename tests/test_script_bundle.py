"""Synthetic fixtures only; no game code or assets required."""
import importlib.util
import struct
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    'inspect_script_bundle', Path(__file__).resolve().parents[1] / 'tools' / 'inspect_script_bundle.py')
inspector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inspector)


def fixture(names=('fixture.map',), chunks=None):
    chunks = chunks or [inspector.MODULE_PREFIX + b'\0' * 8 + b'\x32\x0bGetPosition'] * len(names)
    return (inspector.PREFIX + struct.pack('<I', len(names)) + inspector.TABLE_PREFIX
            + b''.join(struct.pack('<I', len(n)) + n.encode('ascii') for n in names)
            + b'\x01' + b''.join(struct.pack('<I', len(c)) for c in chunks) + b''.join(chunks))


class BundleTests(unittest.TestCase):
    def test_exact_framing_and_identifier_offset(self):
        blob = fixture()
        modules = inspector.parse_bundle(blob)
        self.assertEqual(len(modules), 1)
        item = modules[0]
        self.assertEqual(item['offset'] + item['bytes'], len(blob))
        hits = inspector.identifier_candidates(blob[item['offset']:], item['offset'])
        self.assertEqual(hits, [{'name': 'GetPosition', 'bundleOffset': len(blob) - 11}])

    def test_every_truncated_prefix_is_rejected(self):
        blob = fixture()
        for size in range(len(blob)):
            with self.subTest(size=size), self.assertRaises(ValueError):
                inspector.parse_bundle(blob[:size])

    def test_unknown_version_is_rejected(self):
        blob = bytearray(fixture())
        blob[4] ^= 1
        with self.assertRaises(ValueError):
            inspector.parse_bundle(bytes(blob))

    def test_duplicate_module_names_are_rejected(self):
        with self.assertRaises(ValueError):
            inspector.parse_bundle(fixture(('fixture.map', 'fixture.map')))

    def test_trailing_data_is_rejected(self):
        with self.assertRaises(ValueError):
            inspector.parse_bundle(fixture() + b'\0')

    def test_wrong_module_header_is_rejected(self):
        with self.assertRaises(ValueError):
            inspector.parse_bundle(fixture(chunks=[b'wrong-header----']))

    def test_module_count_budget_is_enforced(self):
        blob = inspector.PREFIX + struct.pack('<I', inspector.MAX_MODULES + 1)
        with self.assertRaises(ValueError):
            inspector.parse_bundle(blob)

    def test_plain_text_and_truncated_identifier_are_not_hits(self):
        self.assertEqual(inspector.identifier_candidates(b'GetPosition\x32\x0bGetPos', 0), [])


if __name__ == '__main__':
    unittest.main()
