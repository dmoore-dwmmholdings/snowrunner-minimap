import sys,unittest,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from candidate_feed import correct_truck_axis
from candidate_feed import replace_packet,resume_sequence,encode_packet,EXPECTED_SHA256
class PublishingTests(unittest.TestCase):
    def test_lateral_axis_adapter(self):
        for lateral,forward in [([0,0,1],[1,0,0]),([1,0,0],[0,0,-1]),([0,0,-1],[-1,0,0]),([-1,0,0],[0,0,1])]:
            pose={'positionCandidateXYZ':[1,2,3],'orientationCandidateXYZ':lateral}
            self.assertEqual(correct_truck_axis(pose)['orientationCandidateXYZ'],forward)
            self.assertEqual(pose['orientationCandidateXYZ'],lateral)
    def test_actual_replace(self):
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory);(p/'old').write_bytes(b'old');(p/'new').write_bytes(b'new')
            replace_packet(p/'new',p/'old');self.assertEqual((p/'old').read_bytes(),b'new')
    def test_transient_conflict(self):
        calls=[];delays=[]
        def replace(a,b):
            calls.append(1)
            if len(calls)<3:
                e=PermissionError('sharing');e.winerror=32;raise e
        replace_packet('a','b',replace,delays.append)
        self.assertEqual(len(calls),3);self.assertEqual(delays,[.025,.025])
    def test_bounded_conflict(self):
        delays=[]
        def replace(a,b):
            e=PermissionError('access');e.winerror=5;raise e
        with self.assertRaises(PermissionError):replace_packet('a','b',replace,delays.append)
        self.assertEqual(len(delays),10)
    def test_unrelated_error(self):
        def replace(a,b):raise FileNotFoundError('missing')
        with self.assertRaises(FileNotFoundError):replace_packet('a','b',replace,lambda _:self.fail('must not retry'))
    def test_resume_context(self):
        p=encode_packet(42,99,123,1000,EXPECTED_SHA256,'level_test_01',None)
        self.assertEqual(resume_sequence(p,42,99,'level_test_01'),123)
        for pid,session,level in ((43,99,'level_test_01'),(42,100,'level_test_01'),(42,99,'level_test_02')):
            with self.assertRaises(ValueError):resume_sequence(p,pid,session,level)
        with self.assertRaises(ValueError):resume_sequence(p[:-1],42,99,'level_test_01')
if __name__=='__main__':unittest.main()
