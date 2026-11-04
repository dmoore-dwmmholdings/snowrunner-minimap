import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from feed_launcher import (load_manifest,feed_command,companion_commands,matching_status,
                           owned_processes_finished,owned_status,launch_companions)


class FeedLauncherTests(unittest.TestCase):
    def manifest(self,folder):
        root=Path(folder);layout=root/'layout with spaces.json';layout.write_text('{}')
        return {'schema':'srmm-launch-v1','pid':12,'session':34,'map':'level_test_01','layout':str(layout),'output_dir':str(root/'new output')}

    def test_paths_remain_separate_arguments_and_no_generic_commands(self):
        with tempfile.TemporaryDirectory() as folder:
            config=self.manifest(folder);path=Path(folder)/'manifest.json';path.write_text(json.dumps(config))
            checked=load_manifest(path);command=feed_command(checked,Path(folder)/'own tools','python.exe')
            self.assertIn(config['layout'],command);self.assertIn(config['output_dir'],command)
            self.assertIn('--until-game-exit',command);self.assertNotIn('--resume',command)
            config['command']='anything';path.write_text(json.dumps(config))
            with self.assertRaises(ValueError):load_manifest(path)

    def test_unrelated_existing_directory_is_not_reused(self):
        with tempfile.TemporaryDirectory() as folder:
            config=self.manifest(folder);config['seconds']=10;Path(config['output_dir']).mkdir()
            with self.assertRaises(ValueError):feed_command(config,Path(folder),'python.exe')
            (Path(config['output_dir'])/'session.local.json').write_text('{}')
            (Path(config['output_dir'])/'candidate.local.bin').write_bytes(bytes(256))
            self.assertIn('--resume',feed_command(config,Path(folder),'python.exe'))

    def test_status_freshness_and_identity(self):
        config={'pid':12,'session':34};status={'schema':'srmm-feed-status-v1','pid':12,'session':34,'run':'a'*32,'tick_ms':1000}
        self.assertTrue(matching_status(status,config,1200))
        for now in (999,3001):self.assertFalse(matching_status(status,config,now))
        status['pid']=13;self.assertFalse(matching_status(status,config,1200))

    def test_companions_are_original_modules_bound_to_status(self):
        config={'pid':12,'session':34,'seconds':28800,'output_dir':'C:/fixture/feed','config':'C:/fixture/session.ini','registry':'C:/fixture/maps.json','save':'C:/fixture/CompleteSave1.cfg','zone_catalog':'C:/fixture/zones.json'}
        commands=companion_commands(config,Path('C:/original/tools'),'python.exe')
        self.assertEqual(len(commands),2)
        for label,command in commands:
            self.assertIn('--feed-status',command);self.assertIn('28800',command)
        self.assertIn('alignment preview',commands[1][0])
        self.assertIn('--zone-catalog',commands[1][1]);self.assertIn('C:/fixture/zones.json',commands[1][1])


    def test_restart_and_close_wait_for_previous_companions(self):
        class Process:
            def __init__(self,code):self.code=code
            def poll(self):return self.code
        feed=Process(0);helper=Process(None)
        self.assertFalse(owned_processes_finished(feed,[('map watcher',helper)]))
        helper.code=0
        self.assertTrue(owned_processes_finished(feed,[('map watcher',helper)]))
        feed.code=None
        self.assertFalse(owned_processes_finished(feed,[('map watcher',helper)]))

    def test_partial_companion_failure_keeps_started_process_owned(self):
        first=object();companions=[];calls=[]
        def launch(command,label):
            calls.append(command)
            if len(calls)==2:raise OSError('owned fixture launch failure')
            return first
        with self.assertRaises(OSError):
            launch_companions([('map',['map']),('destinations',['destinations'])],launch,companions)
        self.assertEqual(companions,[('map',first)])

    def test_close_cannot_stop_replacement_or_unowned_feed(self):
        class Process:pid=56
        config={'pid':12,'session':34}
        status={'schema':'srmm-feed-status-v1','pid':12,'session':34,'publisher_pid':56,'run':'a'*32,'tick_ms':1000}
        self.assertTrue(owned_status(status,config,Process(),'a'*32,1100))
        self.assertTrue(owned_status(status,config,Process(),None,1100))
        self.assertFalse(owned_status(status,config,None,None,1100))
        self.assertFalse(owned_status(status,config,Process(),'b'*32,1100))
        status['publisher_pid']=57
        self.assertFalse(owned_status(status,config,Process(),None,1100))


if __name__=='__main__':unittest.main()
