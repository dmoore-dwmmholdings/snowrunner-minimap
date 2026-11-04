import ctypes,hashlib,json,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from startup_session import supported_layout,session_fields,prepare_session,worker_command,starter_lock
from candidate_feed import select_roots

class StartupTests(unittest.TestCase):
    def registry(self,root):
        asset=root/'map.dds';asset.write_bytes(bytes(144))
        p=root/'registry.json';p.write_text(json.dumps({'schema':'srmm-map-registry-v1','maps':[
            dict(map='level_test_01',map_path=str(asset),map_sha256=hashlib.sha256(asset.read_bytes()).hexdigest(),affine=[.001,0,.5,0,-.001,.5],uv_per_pixel=.001,geometry='alignment_preview',texture_v_flipped=True)]}))
        return p
    def test_build_layout_has_no_session_or_absolute_pointer(self):
        report=supported_layout()
        self.assertEqual(select_roots(report,{'.data':(44000000,1000000)}),(44645368,44405056))
        self.assertNotIn('pid',report);self.assertNotIn('moduleBase',report)
        report['executableSha256']='0'*64
        with self.assertRaises(ValueError):select_roots(report,{'.data':(44000000,1000000)})
    def test_bundle_dispatch_rejects_arbitrary_script(self):
        cmd=['minimap.exe','C:/bundle/candidate_feed.py','--pid','12']
        self.assertEqual(worker_command(cmd,'log.txt',True),['minimap.exe','--worker','candidate_feed','--log','log.txt','--pid','12'])
        self.assertEqual(worker_command(cmd,'log.txt',False),cmd)
        with self.assertRaises(ValueError):worker_command(['minimap.exe','evil.py'],'log.txt',True)
    def test_preparation_uses_fresh_identity_preserves_display_only(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);reg=self.registry(root);config=root/'session.local.ini'
            old='version=1\nenabled=0\nsession=44\nmap=level_other_01\npacket_path='+str(root/'old/p.bin')+'\nviewport=12 14 256\nzoom=2\ntracking=north\nshape=square\nui_state_path=stale.bin\n'
            config.write_text(old)
            manifest=prepare_session(root,reg,22)
            m=json.loads(manifest.read_text());new=config.read_text()
            self.assertEqual(m['pid'],22);self.assertNotEqual(m['session'],44)
            self.assertFalse(m['record']);self.assertNotIn('save',m)
            self.assertFalse(Path(m['output_dir']).exists())
            self.assertIn('viewport=12 14 256',new);self.assertIn('tracking=north',new);self.assertNotIn('ui_state_path',new)
            self.assertEqual((manifest.parent/'previous-session.local.ini').read_text(),old)
    def test_bad_save_does_not_change_config(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);reg=self.registry(root);p=root/'session.local.ini';p.write_text('unchanged')
            with self.assertRaises(ValueError):prepare_session(root,reg,22,root/'CompleteSave.cfg')
            self.assertEqual(p.read_text(),'unchanged')
    def test_manifest_write_failure_preserves_existing_config(self):
        import startup_session
        for fail_write in (1,2):
            with self.subTest(manifest_write=fail_write),tempfile.TemporaryDirectory() as d:
                root=Path(d);reg=self.registry(root);p=root/'session.local.ini'
                old='version=1\nenabled=0\npacket_path='+str(root/'old/p.bin')+'\n';p.write_text(old)
                actual=startup_session.atomic_json;writes=0
                def fail_manifest(path,value):
                    nonlocal writes
                    if path.name=='launch.local.json':
                        writes+=1
                        if writes==fail_write:raise OSError('simulated manifest write failure')
                    return actual(path,value)
                with patch.object(startup_session,'atomic_json',side_effect=fail_manifest):
                    with self.assertRaisesRegex(OSError,'simulated manifest'):prepare_session(root,reg,22)
                self.assertEqual(p.read_text(),old)
    def test_new_config_is_not_published_when_manifest_validation_fails(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);reg=self.registry(root);p=root/'session.local.ini'
            with patch('feed_launcher.load_manifest',side_effect=ValueError('controller rejected manifest')):
                with self.assertRaisesRegex(ValueError,'controller rejected'):prepare_session(root,reg,22)
            self.assertFalse(p.exists())
    def test_refuse_active_feed_before_config_replacement(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);reg=self.registry(root);p=root/'session.local.ini'
            tick=ctypes.WinDLL('kernel32').GetTickCount64;tick.restype=ctypes.c_uint64
            (root/'status.local.json').write_text(json.dumps({'state':'running','tick_ms':tick()}))
            old='version=1\nenabled=1\npacket_path='+str(root/'candidate.local.bin')+'\n';p.write_text(old)
            with self.assertRaises(RuntimeError):prepare_session(root,reg,22)
            self.assertEqual(p.read_text(),old)
    def test_starter_lock_excludes_duplicate_process_work(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            with starter_lock(root):
                with self.assertRaises(OSError):
                    with starter_lock(root):pass
            with starter_lock(root):pass

if __name__=='__main__':unittest.main()
