"""Original synthetic metadata fixtures; no game bytecode or assets."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import inspect_script_metadata as metadata


def varint(value):
    result = bytearray()
    while value > 127:
        result.append((value & 127) | 128)
        value >>= 7
    return bytes(result + bytes([value]))


def integer(number, value):
    return varint(number << 3) + varint(value)


def message(number, value):
    return varint((number << 3) | 2) + varint(len(value)) + value


def fixture(kind=1, body=None, function_name=1, function_index=0, payload_size=None):
    imports = message(1, integer(1, 2))
    reference = integer(2, 1) + integer(3, 0) + integer(4, 5) + integer(5, 2)
    catalog = imports + message(4, reference)
    owner_type = integer(1, 1) + integer(2, 0)
    void_type = integer(1, 3) + integer(2, 0)
    function_type = integer(1, 9) + message(4, integer(1, 1))
    member = integer(1, 1) + integer(2, 5) + integer(3, kind) + integer(4, function_index)
    owner = integer(1, 0) + message(3, member)
    function = integer(1, 2) + message(2, integer(1, function_name))
    if body is not None:
        function += message(5, body)
    payload = (message(4, catalog) + b''.join(message(5, t) for t in
               (owner_type, void_type, function_type)) +
               b''.join(message(6, s) for s in (b'Example', b'Run', b'void')) +
               message(7, owner) + message(8, function))
    return metadata.MODULE_PREFIX + struct.pack('<II', 123, len(payload) if payload_size is None
                                              else payload_size) + payload


class MetadataTests(unittest.TestCase):
    def test_reference_matches_declaration(self):
        result = metadata.inspect_module(fixture())
        declaration, reference = result['declarations'][0], result['references'][0]
        self.assertEqual(declaration['owner'], reference['ownerTypeCandidate'])
        for key in ('name', 'slot', 'shapeCandidate'):
            self.assertEqual(declaration[key], reference[key])
        self.assertFalse(declaration['hasBodyField'])
        self.assertEqual(declaration['shapeCandidate'], {'return': 'void', 'parameters': []})

    def test_body_is_opaque_and_not_emitted(self):
        # Deliberately invalid wire bytes: a body must never be interpreted.
        result = metadata.inspect_module(fixture(kind=2, body=b'\xff\x00\xff'))
        self.assertTrue(result['declarations'][0]['hasBodyField'])
        self.assertNotIn('body', result['declarations'][0])

    def test_empty_body_field_is_present(self):
        self.assertTrue(metadata.inspect_module(fixture(body=b''))['declarations'][0]['hasBodyField'])

    def test_every_truncated_prefix_rejected(self):
        blob = fixture()
        for size in range(len(blob)):
            with self.subTest(size=size), self.assertRaises(ValueError):
                metadata.inspect_module(blob[:size])

    def test_payload_size_and_version(self):
        for blob in (fixture(payload_size=1), b'BAD!' + fixture()[4:]):
            with self.assertRaises(ValueError):
                metadata.inspect_module(blob)

    def test_invalid_function_index(self):
        with self.assertRaisesRegex(ValueError, 'index'):
            metadata.inspect_module(fixture(function_index=7))

    def test_name_disagreement(self):
        with self.assertRaisesRegex(ValueError, 'disagreement'):
            metadata.inspect_module(fixture(function_name=0))

    def test_malformed_wire_fields(self):
        for blob in (b'\x00', b'\x0b', b'\x08\x80', b'\x0a\x03a', b'\x09a',
                     b'\x0da', b'\x08' + b'\xff' * 9 + b'\x02'):
            with self.subTest(blob=blob), self.assertRaises(ValueError):
                metadata.fields(blob)

    def test_fixed_fields_are_bounded(self):
        self.assertEqual(len(metadata.fields(b'\x09' + bytes(8) + b'\x15' + bytes(4))), 2)

    def test_duplicate_or_wrong_wire_singular_rejected(self):
        for record in ([(1, 0, 0), (1, 0, 0)], [(1, 2, b'')]):
            with self.assertRaises(ValueError):
                metadata.one(record, 1, 0)


if __name__ == '__main__':
    unittest.main()
