import importlib.util
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import candidate_feed as feed


class CandidateFeedTests(unittest.TestCase):
    def pose(self):return {'positionCandidateXYZ':(100,0,200),'orientationCandidateXYZ':(0,0,1)}

    def test_wire_offsets_and_zero_padding(self):
        value=feed.encode_packet(42,99,1,1000,'ab'*32,'level_test_01',self.pose())
        self.assertEqual(len(value),256)
        self.assertEqual(struct.unpack_from('<8s4I3Q',value), (b'SRMMTEL1',1,256,1,42,1,1000,99))
        self.assertEqual(value[48:61],b'level_test_01')
        self.assertEqual(value[61:112],bytes(51))
        self.assertEqual(value[112:144],bytes.fromhex('ab'*32))
        self.assertEqual(struct.unpack_from('<6d',value,144),(100,0,200,0,0,1))
        self.assertEqual(value[192:],bytes(64))

    def test_unavailable_contains_no_pose(self):
        value=feed.encode_packet(42,99,2,1001,'00'*32,'level_test_01',None)
        self.assertEqual(struct.unpack_from('<I',value,16)[0],0)
        self.assertEqual(value[144:],bytes(112))

    def test_bad_identity_and_coordinates_rejected(self):
        for pid,session,sequence,tick,build,level in [(0,1,1,1,'ab'*32,'level_test_01'),(1,0,1,1,'ab'*32,'level_test_01'),(1,1,0,1,'ab'*32,'level_test_01'),(1,1,1,-1,'ab'*32,'level_test_01'),(1,1,1,1,'gg'*32,'level_test_01'),(1,1,1,1,'ab'*32,'../map')]:
            with self.assertRaises(ValueError):feed.encode_packet(pid,session,sequence,tick,build,level,None)
        for pose in [{'positionCandidateXYZ':(float('nan'),0,0),'orientationCandidateXYZ':(0,0,1)},{'positionCandidateXYZ':(0,0,0),'orientationCandidateXYZ':(0,1,0)},{'positionCandidateXYZ':(0,0,0),'orientationCandidateXYZ':(0,0,0)}]:
            with self.assertRaises(ValueError):feed.encode_packet(1,1,1,1,'ab'*32,'level_test_01',pose)

    def report(self):return {'schema':'srmm-live-candidates-v1','executableSha256':feed.EXPECTED_SHA256,'discovery':{'completeWithinBound':True,'truckCandidates':[{'rootRva':4096}],'levelCandidates':[{'rootRva':4104}]}}

    def test_prior_roots_require_unique_complete_bounded_report(self):
        self.assertEqual(feed.select_roots(self.report(),{'.data':(4096,16)}),(4096,4104))
        for rva in (4095,4097,4112,True):
            report=self.report();report['discovery']['truckCandidates'][0]['rootRva']=rva
            with self.assertRaises(ValueError):feed.select_roots(report,{'.data':(4096,16)})
        report=self.report();report['discovery']['completeWithinBound']=False
        with self.assertRaises(ValueError):feed.select_roots(report,{'.data':(4096,16)})

    def test_fixed_sampling_rechecks_both_roots(self):
        # Mock only the bounded primitive reads, never any operating-system API.
        with patch.object(feed,'pointer',side_effect=[10,20,10,20]),patch.object(feed,'truck_type',return_value=True),patch.object(feed,'level_name',return_value='level_test_01'),patch.object(feed,'read_pose',return_value=self.pose()):
            name,pose=feed.sample_fixed(object(),100,1000,(8,16));self.assertEqual(name,'level_test_01');self.assertEqual(pose,self.pose())
        with patch.object(feed,'pointer',side_effect=[10,20,11]),patch.object(feed,'truck_type',return_value=True),patch.object(feed,'level_name',return_value='level_test_01'),patch.object(feed,'read_pose',return_value=self.pose()):
            self.assertEqual(feed.sample_fixed(object(),100,1000,(8,16)),(None,None))
        with patch.object(feed,'pointer',side_effect=[10,20,10,21]),patch.object(feed,'truck_type',return_value=True),patch.object(feed,'level_name',return_value='level_test_01'),patch.object(feed,'read_pose',return_value=self.pose()):
            self.assertEqual(feed.sample_fixed(object(),100,1000,(8,16)),(None,None))

    def test_type_and_level_transition_hide_candidate(self):
        with patch.object(feed,'pointer',side_effect=[10,20,10,20]),patch.object(feed,'truck_type',return_value=True),patch.object(feed,'level_name',side_effect=['level_test_01','level_test_02']),patch.object(feed,'read_pose',return_value=self.pose()):
            self.assertEqual(feed.sample_fixed(object(),100,1000,(8,16)),(None,None))
        with patch.object(feed,'pointer',side_effect=[10,20]),patch.object(feed,'truck_type',return_value=False):
            self.assertEqual(feed.sample_fixed(object(),100,1000,(8,16)),(None,None))


if __name__=='__main__':unittest.main()
