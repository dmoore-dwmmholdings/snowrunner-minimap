"""Synthetic address-space fixtures. Never opens the game or another process."""
import math
import ctypes
import os
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import probe_live_pose as probe


class Memory:
    def __init__(self):
        self.blocks = {}

    def read(self, address, length):
        for base, blob in self.blocks.items():
            offset = address - base
            if 0 <= offset and offset + length <= len(blob):
                return bytes(blob[offset:offset + length])
        return None


def fixture():
    memory = Memory()
    base, root = 0x10000000, 0x20000000
    image = bytearray(0x5000)
    obj = bytearray(128)
    struct.pack_into('<Q', obj, 0, base + 0x1008)
    struct.pack_into('<Q', image, 0x1000, base + 0x2000)
    struct.pack_into('<6I', image, 0x2000, 1, 0, 0, 0x3000, 0, 0x2000)
    image[0x3010:0x3010 + len(probe.TRUCK_RTTI)] = probe.TRUCK_RTTI
    struct.pack_into('<Q', image, 0x4000, root)
    memory.blocks[base] = image
    memory.blocks[root] = obj
    current = root
    for index, offset in enumerate((0x08, 0x60, 0x68)):
        following = root + 0x1000 * (index + 1)
        struct.pack_into('<Q', memory.blocks[current], offset, following)
        memory.blocks[following] = bytearray(208)
        current = following
    struct.pack_into('<3f', memory.blocks[current], 0xb0, 1, 0, 0)
    struct.pack_into('<3f', memory.blocks[current], 0xc0, 12, 3, 45)
    return memory, base, root, current


class ProbeTests(unittest.TestCase):
    def test_exact_rtti_and_pose_candidates(self):
        memory, base, root, _ = fixture()
        self.assertTrue(probe.truck_type(memory, base, 0x5000, root))
        self.assertEqual(probe.read_pose(memory, base + 0x4000)['positionCandidateXYZ'], (12, 3, 45))

    def test_bad_locator_or_type_never_matches(self):
        for offset, value in ((0x2000, 0), (0x2014, 42), (0x200c, 0xffffffff)):
            memory, base, root, _ = fixture()
            struct.pack_into('<I', memory.blocks[base], offset, value)
            self.assertFalse(probe.truck_type(memory, base, 0x5000, root))
        memory, base, root, _ = fixture()
        memory.blocks[base][0x3010] = ord('X')
        self.assertFalse(probe.truck_type(memory, base, 0x5000, root))

    def test_pose_rejects_missing_or_nonfinite_or_bad_orientation(self):
        for offset, value in ((0xc0, math.nan), (0xc0, 30000), (0xb0, 0)):
            memory, base, _, body = fixture()
            struct.pack_into('<f', memory.blocks[body], offset, value)
            self.assertIsNone(probe.read_pose(memory, base + 0x4000))
        memory, base, _, body = fixture()
        del memory.blocks[body]
        self.assertIsNone(probe.read_pose(memory, base + 0x4000))

    def test_changed_pointer_chain_rejected(self):
        memory, base, root, _ = fixture()
        original = memory.read
        reads = 0
        def changing(address, length):
            nonlocal reads
            if address == root + 8:
                reads += 1
                if reads == 2:
                    return bytes(8)
            return original(address, length)
        memory.read = changing
        self.assertIsNone(probe.read_pose(memory, base + 0x4000))

    def test_level_schema_bounds(self):
        memory = Memory()
        blob = bytearray(56)
        memory.blocks[0x20000000] = blob
        name = b'level_test_01'
        struct.pack_into('<I', blob, 16, len(name))
        blob[24:24 + len(name)] = name
        self.assertEqual(probe.level_name(memory, 0x20000000), name.decode())
        blob[24 + len(name)] = 1
        self.assertIsNone(probe.level_name(memory, 0x20000000))
        struct.pack_into('<I', blob, 16, 200)
        self.assertIsNone(probe.level_name(memory, 0x20000000))

    def test_discovery_reports_incomplete_and_ambiguity(self):
        memory, base, root, _ = fixture()
        result = probe.discover(memory, base, 0x5000, (0x4000, 0x1000))
        self.assertTrue(result['completeWithinBound'])
        self.assertEqual(len(result['truckCandidates']), 1)
        result = probe.discover(memory, base, 0x5000, (0x4000, 0x2000))
        self.assertFalse(result['completeWithinBound'])
        # A distinct object with the same RTTI must remain a second candidate.
        memory.blocks[root + 0x9000] = memory.blocks[root][:]
        struct.pack_into('<Q', memory.blocks[base], 0x4008, root + 0x9000)
        result = probe.discover(memory, base, 0x5000, (0x4000, 0x1000))
        self.assertEqual(len(result['truckCandidates']), 2)

    def test_pointer_budget_never_reports_complete(self):
        memory, base, root, _ = fixture()
        struct.pack_into('<Q', memory.blocks[base], 0x4008, root + 0x9000)
        old = probe.MAX_POINTERS
        try:
            probe.MAX_POINTERS = 1
            self.assertFalse(probe.discover(memory, base, 0x5000, (0x4000, 0x1000))['completeWithinBound'])
        finally:
            probe.MAX_POINTERS = old

    def test_pe_section_bounds(self):
        blob = bytearray(1024)
        blob[:2] = b'MZ'
        struct.pack_into('<I', blob, 60, 128)
        blob[128:132] = b'PE\0\0'
        struct.pack_into('<HH', blob, 132, 0x8664, 1)
        struct.pack_into('<H', blob, 148, 240)
        struct.pack_into('<H', blob, 152, 0x20b)
        struct.pack_into('<I', blob, 208, 0x5000)
        blob[392:400] = b'.data\0\0\0'
        struct.pack_into('<II', blob, 400, 0x1000, 0x4000)
        self.assertEqual(probe.image_sections(blob), (0x5000, {'.data': (0x4000, 0x1000)}))
        struct.pack_into('<I', blob, 400, 0x2000)
        with self.assertRaises(ValueError):
            probe.image_sections(blob)

    @unittest.skipUnless(os.name == 'nt' and ctypes.sizeof(ctypes.c_void_p) == 8, 'Windows x64 only')
    def test_windows_reader_own_process_only(self):
        reader = probe.WindowsReader(os.getpid())
        try:
            data = ctypes.create_string_buffer(b'original fixture')
            self.assertEqual(reader.read(ctypes.addressof(data), 16), b'original fixture')
            self.assertIsNone(reader.read(0, 8))
            self.assertTrue(reader.executable().exists())
            self.assertTrue(reader.modules())
        finally:
            reader.close()


if __name__ == '__main__':
    unittest.main()
