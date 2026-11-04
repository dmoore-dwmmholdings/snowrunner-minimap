import sys,unittest,tempfile,hashlib,json
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from map_switcher import choose,packet_map,replace_if_unchanged,load_registry
from candidate_feed import encode_packet
from probe_live_pose import EXPECTED_SHA256

class MapSwitchTests(unittest.TestCase):
    def test_packet_identity_freshness(self):
        pose={'positionCandidateXYZ':[1,2,3],'orientationCandidateXYZ':[0,0,1]}
        packet=encode_packet(12,34,1,1000,EXPECTED_SHA256,'level_test_01',pose)
        self.assertEqual(packet_map(packet,12,34,1100),'level_test_01')
        for pid,session,now in ((13,34,1100),(12,35,1100),(12,34,999),(12,34,1751)):
            self.assertIsNone(packet_map(packet,pid,session,now))
        self.assertIsNone(packet_map(encode_packet(12,34,2,1100,EXPECTED_SHA256,'level_test_01',None),12,34,1100))
    def test_unknown_map_hides_then_registered_map_recovers(self):
        first={'map':'level_test_01','enabled':'1','zoom':'2','packet_path':'fixed','session':'34'}
        registry={'level_test_02':{'map':'level_test_02','map_path':'second.dds','geometry':'alignment_preview'}}
        hidden=choose(first,registry,'level_unknown')
        self.assertEqual(hidden['enabled'],'0')
        recovered=choose(hidden,registry,'level_test_02')
        self.assertEqual(recovered['enabled'],'1');self.assertEqual(recovered['map_path'],'second.dds')
        self.assertEqual(recovered['zoom'],'2');self.assertEqual(recovered['session'],'34')
        self.assertEqual(recovered['geometry'],'alignment_preview')
    def test_changed_settings_are_not_overwritten(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'session.ini';path.write_bytes(b'zoom=3\n')
            self.assertFalse(replace_if_unchanged(path,b'zoom=2\n',{'zoom':'1'}))
            self.assertEqual(path.read_bytes(),b'zoom=3\n')
            self.assertTrue(replace_if_unchanged(path,b'zoom=3\n',{'zoom':'4'}))
            self.assertEqual(set(Path(folder).iterdir()),{path,path.with_name('session.local.lock')})
    def test_registry_fingerprint_and_calibration(self):
        with tempfile.TemporaryDirectory() as folder:
            asset=Path(folder)/'fixture.dds';asset.write_bytes(bytes(144))
            entry=dict(map='level_test_01',map_path=str(asset),map_sha256=hashlib.sha256(asset.read_bytes()).hexdigest(),affine=[.001,0,.5,0,-.001,.5],uv_per_pixel=.001,texture_v_flipped=True,geometry='alignment_preview')
            path=Path(folder)/'registry.json'
            def save():path.write_text(json.dumps({'schema':'srmm-map-registry-v1','maps':[entry]}))
            save();self.assertEqual(load_registry(path)['level_test_01']['geometry'],'alignment_preview')
            entry['map_sha256']='a'*64;save()
            with self.assertRaises(ValueError):load_registry(path)

if __name__=='__main__':unittest.main()
