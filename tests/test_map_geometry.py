import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from map_geometry import (find_terrain_bounds,height_header_dimensions,inspect_geometry,
                          check_map_dimensions,TerrainBounds)


def header(width=481,depth=241):
    data=b''
    for key,value in ((0xf0,b'TCIP'),(0x102,struct.pack('<4I',width,depth,1,1)),
                      (0xf2,struct.pack('<I',0x37)),(0xf9,struct.pack('<I',1)),
                      (0x107,struct.pack('<2I',0,width*depth*2)),(0x104,bytes(4)),(1,b'')):
        data+=struct.pack('<HI',key,len(data)+6+len(value))+value
    return data


def record(width=240.,depth=120.,x=-100.,z=-50.,u=10,v=5):
    return struct.pack('<4f2I',width,depth,x,z,u,v)


class GeometryTests(unittest.TestCase):
    def test_header_framing_dimensions(self):
        self.assertEqual(height_header_dimensions(header()),(481,241))
        for data in (header()[:-1],header()+b'x',header(480,241)):
            with self.assertRaises(ValueError):height_header_dimensions(data)

    def test_rectangular_noncentered_origin_is_preserved(self):
        bounds=find_terrain_bounds(bytes(37)+record()+bytes(11),(481,241))
        self.assertEqual((bounds.width,bounds.depth,bounds.origin_x,bounds.origin_z),(240.,120.,-100.,-50.))
        a=bounds.affine
        def uv(x,z):return a[0]*x+a[1]*z+a[2],a[3]*x+a[4]*z+a[5]
        self.assertAlmostEqual(uv(-100,70)[0],0.)
        self.assertAlmostEqual(uv(-100,70)[1],0.)
        self.assertAlmostEqual(uv(140,-50)[0],1.)
        self.assertAlmostEqual(uv(140,-50)[1],1.)

    def test_duplicate_record_and_wrong_grid_are_rejected(self):
        with self.assertRaises(ValueError):find_terrain_bounds(bytes(16)+record()+record(),(481,241))
        with self.assertRaises(ValueError):find_terrain_bounds(bytes(16)+record(),(529,241))
        with self.assertRaises(ValueError):find_terrain_bounds(bytes(16)+record(width=241.),(481,241))

    def test_nonfinite_origin_and_size_bound(self):
        with self.assertRaises(ValueError):find_terrain_bounds(bytes(16)+record(x=float('nan')),(481,241))
        with self.assertRaises(ValueError):find_terrain_bounds(bytes(65537),(481,241))

    def test_image_must_match_full_terrain_layout(self):
        bounds=TerrainBounds(240,120,-120,-60,10,5,32)
        check_map_dimensions(bounds,(480,240))
        for dimensions in ((240,240),(480,480),(481,241)):
            with self.assertRaises(ValueError):check_map_dimensions(bounds,dimensions)

    def test_local_archive_evidence_and_duplicate_rejection(self):
        with tempfile.TemporaryDirectory() as folder:
            pak=Path(folder)/'level_test_01.pak'
            with zipfile.ZipFile(pak,'w') as archive:
                archive.writestr('[textures]/pct/level_test_01_height_map.pct_header',header())
                archive.writestr('[scene]/data_for_ssl.bin',bytes(37)+record()+bytes(11))
            bounds,evidence=inspect_geometry(pak,'level_test_01')
            self.assertEqual(bounds.offset,37)
            self.assertFalse(evidence['runtime_alignment_verified'])
            self.assertEqual(evidence['height_nodes'],[481,241])
            self.assertEqual(len(evidence['prefix_sha256']),64)
            with self.assertRaises(ValueError):inspect_geometry(pak,'level_other_01')


if __name__=='__main__':unittest.main()
