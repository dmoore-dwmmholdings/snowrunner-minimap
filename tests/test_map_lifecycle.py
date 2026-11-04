import sys
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from contextlib import contextmanager
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import map_switcher as maps
from minimap_settings import update_display
from session_config import config_lock


class MapLifecycleTests(unittest.TestCase):
    def fields(self):
        return {'version':'1','enabled':'1','session':'34','packet_path':'C:/private/packet.bin',
                'map':'level_test_01','map_path':'old.dds','zoom':'1','tracking':'north','shape':'circle','viewport':'32 32 320'}

    def create(self,folder):
        path=Path(folder)/'session.local.ini'
        path.write_text(''.join(f'{k}={v}\n' for k,v in self.fields().items()))
        return path

    def registry(self):
        return {'level_test_02':{'map':'level_test_02','map_path':'second.dds','geometry':'alignment_preview'}}

    def test_map_retry_preserves_intervening_display_edit(self):
        with tempfile.TemporaryDirectory() as folder:
            path=self.create(folder);before,fields=maps.read_fields(path)
            selected=maps.choose(fields,self.registry(),'level_test_02')
            update_display(path,2,'motion','square',40,50,360)
            self.assertFalse(maps.replace_if_unchanged(path,before,selected))
            retry,current=maps.read_fields(path)
            self.assertTrue(maps.replace_if_unchanged(path,retry,maps.choose(current,self.registry(),'level_test_02')))
            _,result=maps.read_fields(path)
            self.assertEqual(result['map_path'],'second.dds');self.assertEqual(result['zoom'],'2.0')
            self.assertEqual(result['shape'],'square');self.assertEqual(result['viewport'],'40.0 50.0 360.0')

    def test_unknown_map_allows_settings_edit_and_recovers_without_reset(self):
        with tempfile.TemporaryDirectory() as folder:
            path=self.create(folder);before,fields=maps.read_fields(path)
            self.assertTrue(maps.replace_if_unchanged(path,before,maps.choose(fields,self.registry(),'level_unknown')))
            update_display(path,3,'heading','square',60,70,400)
            before,fields=maps.read_fields(path);self.assertEqual(fields['enabled'],'0')
            maps.replace_if_unchanged(path,before,maps.choose(fields,self.registry(),'level_test_02'))
            _,result=maps.read_fields(path)
            self.assertEqual(result['enabled'],'1');self.assertEqual(result['zoom'],'3.0')
            self.assertEqual(result['tracking'],'heading');self.assertEqual(result['session'],'34')

    def test_missing_pose_does_not_change_map_binding_or_enabled_state(self):
        for enabled in ('0','1'):
            fields=self.fields();fields['enabled']=enabled
            self.assertEqual(maps.choose(fields,self.registry(),None),fields)

    def test_terminal_disable_retries_with_fresh_settings(self):
        with tempfile.TemporaryDirectory() as folder:
            path=self.create(folder);calls=[]
            @contextmanager
            def contend(p):
                calls.append(1)
                if len(calls)==1:
                    update_display(path,4,'motion','square',80,90,420)
                    raise BlockingIOError('owned contention fixture')
                with config_lock(p):yield
            with patch.object(maps,'config_lock',contend):
                self.assertTrue(maps.disable_owned_session(path,34,'C:/private/packet.bin',pause=lambda _:None))
            _,result=maps.read_fields(path)
            self.assertEqual(len(calls),2);self.assertEqual(result['enabled'],'0')
            self.assertEqual(result['zoom'],'4.0');self.assertEqual(result['viewport'],'80.0 90.0 420.0')

    def test_terminal_disable_does_not_touch_replacement_session(self):
        with tempfile.TemporaryDirectory() as folder:
            path=self.create(folder);before=path.read_bytes()
            self.assertFalse(maps.disable_owned_session(path,35,'C:/private/packet.bin'))
            self.assertEqual(path.read_bytes(),before)

    def test_status_must_be_fresh_typed_exact_and_bound_before_use(self):
        valid={'schema':'srmm-feed-status-v1','pid':12,'session':34,'run':'a'*32,'state':'running','tick_ms':1000}
        self.assertEqual(maps.valid_feed_run(valid,12,34,1100),'a'*32)
        invalid=[[],{},dict(valid,run='garbage'),dict(valid,run=42),dict(valid,pid=True),dict(valid,tick_ms=True),
                 dict(valid,tick_ms=1101),dict(valid,tick_ms=-1001),dict(valid,state='game_exited'),dict(valid,session=35)]
        for status in invalid:self.assertIsNone(maps.valid_feed_run(status,12,34,1100))
        self.assertIsNone(maps.valid_feed_run(valid,12,34,1100,'b'*32))

    def test_actual_second_process_cannot_write_while_config_lock_held(self):
        with tempfile.TemporaryDirectory() as folder:
            path=self.create(folder);before=path.read_bytes()
            script="""import sys
sys.path.insert(0,sys.argv[1])
from minimap_settings import update_display
from pathlib import Path
try:
 update_display(Path(sys.argv[2]),2,'motion','square',40,50,360)
 print('wrote')
except (PermissionError,BlockingIOError):
 print('busy')
"""
            tools=str(Path(maps.__file__).parent)
            with config_lock(path):
                result=subprocess.run([sys.executable,'-c',script,tools,str(path)],capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stderr);self.assertEqual(result.stdout.strip(),'busy')
            self.assertEqual(path.read_bytes(),before)
            update_display(path,2,'motion','square',40,50,360)
            self.assertEqual(maps.read_fields(path)[1]['zoom'],'2.0')


if __name__=='__main__':unittest.main()
