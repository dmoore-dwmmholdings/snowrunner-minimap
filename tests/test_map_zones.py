import math
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from map_zones import parse_zones


def record(identity='ZONE_ONE',x=12.,z=34.,variant=0):
    data=struct.pack('<2I5f',2,variant,x,z,1.,0.,-1.)
    for text in (identity,'fixtureIcon','fixtureIconSmall','ZONE_LABEL'):
        encoded=text.encode()+b'\0';data+=struct.pack('<H',len(encoded))+encoded
    tail=(2.,.75,-1.,1.,1.,1.,1.,0.,10.) if variant==1 else (2.,.75,-1.,1.,1.,1.,1.,0.,10.,20.,1.5)
    return data+struct.pack('<B'+str(len(tail))+'f',1,*tail)


def scene(*records):return bytes(40)+struct.pack('<I',len(records))+b''.join(records)+bytes(4)


class ZoneTests(unittest.TestCase):
    def test_complete_counted_table_uses_identifier_not_label(self):
        zones=parse_zones(scene(record(),record('ZONE_TWO',-90.,-20.)))
        self.assertEqual([(z['id'],z['x'],z['z']) for z in zones],[('ZONE_ONE',12.,34.),('ZONE_TWO',-90.,-20.)])
        self.assertEqual(zones[0]['kind'],'static_zone')
        self.assertEqual(zones[0]['label'],'ZONE_LABEL')

    def test_partial_table_count_and_unknown_trailer_rejected(self):
        original=scene(record(),record('ZONE_TWO'))
        for data in (original[:-1],original+b'\0',original[:40]+struct.pack('<I',1)+original[44:]):
            with self.assertRaises(ValueError):parse_zones(data)

    def test_unknown_variant_duplicate_and_nonfinite_rejected(self):
        for data in (scene(record(variant=2)),scene(record(),record()),scene(record(x=math.nan))):
            with self.assertRaises(ValueError):parse_zones(data)

    def test_nearby_string_without_record_chain_is_not_geometry(self):
        data=bytes(40)+b'ZONE_ONE\0'+struct.pack('<5f',12.,34.,1.,0.,-1.)+bytes(4)
        with self.assertRaises(ValueError):parse_zones(data)

    def test_round_zone_and_counted_named_groups(self):
        def text(value):
            raw=value.encode()+b'\0';return struct.pack('<H',len(raw))+raw
        groups=struct.pack('<I',1)+text('FixtureGroup')+struct.pack('<I',1)+text('ZONE_ONE')
        groups+=struct.pack('<I',2)+struct.pack('<I',0)+struct.pack('<3I',2,12,15)
        source=scene(record(variant=1),record('ZONE_TWO'))[:-4]+groups
        self.assertEqual(len(parse_zones(source)),2)
        with self.assertRaises(ValueError):parse_zones(source[:-1])


if __name__=='__main__':unittest.main()
