import json
import math
import os
import ctypes
from pathlib import Path
import struct
import sys
import subprocess
import tempfile
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from destination_source import (MAX_POINTS, PACKET_BYTES, SavedWaypointSource,
    SavedWaypoints, encode_destinations, parse_saved_waypoints, pose_context, feed_run,
    parse_saved_cargo_targets, add_saved_objective_points, load_zone_catalog)
from candidate_feed import encode_packet
from probe_live_pose import EXPECTED_SHA256


class SavedDestinationTests(unittest.TestCase):
    def objective(self, actions=None, stages=None):
        actions = actions if actions is not None else [{'map':'level_test_01','zones':['ZONE_A'], 'cargoState':{'curValue':0,'aimValue':3}}]
        stages = stages if stages is not None else [{'cargoDeliveryActions':actions,'visitAllZonesState':None}]
        return json.dumps({'CompleteSave1':{'SslValue':{'trackedObjective':'OBJECTIVE_A', 'objectiveStates':{'OBJECTIVE_A':{'id':'OBJECTIVE_A','isFinished':False,'stagesState':stages}}}}}).encode()

    def data(self, entries, level='level_test_01'):
        return json.dumps({'CompleteSave1':{'SslValue':{'waypoints':{level:entries}}}}).encode()

    def point(self, x=1, z=-2):
        return {'type':0,'point':{'x':x,'y':float('nan'),'z':z},'modelHeightBounds':None}

    def test_explicit_slot_map_and_xz_only_conversion(self):
        saved=parse_saved_waypoints(self.data([self.point()])+b'\0', 'CompleteSave1', 'level_test_01', 123)
        self.assertEqual(saved.points, ((150.,-300.),));self.assertEqual(saved.saved_unix_ms,123)
        self.assertEqual(parse_saved_waypoints(self.data([self.point()]),'CompleteSave1','level_other_01',123).points,())
        with self.assertRaises(KeyError):parse_saved_waypoints(self.data([]),'CompleteSave','level_test_01',123)

    def test_invalid_positions_kinds_and_duplicate_fields_fail_closed(self):
        for point in [self.point(float('inf')),self.point(True),self.point(200),dict(self.point(),type=1)]:
            with self.assertRaises(ValueError):parse_saved_waypoints(self.data([point]),'CompleteSave1','level_test_01',123)
        data=b'{"CompleteSave1":{},"CompleteSave1":{}}'
        with self.assertRaises(ValueError):parse_saved_waypoints(data,'CompleteSave1','level_test_01',123)

    def test_route_order_truncation_is_bounded(self):
        saved=parse_saved_waypoints(self.data([self.point(n,0) for n in range(20)]),'CompleteSave1','level_test_01',123)
        self.assertEqual(len(saved.points),MAX_POINTS);self.assertEqual(saved.points[-1],(2250.,0.))
        with self.assertRaises(ValueError):parse_saved_waypoints(self.data([self.point()]*1025),'CompleteSave1','level_test_01',123)

    def test_packet_offsets_identity_and_zero_padding(self):
        saved=SavedWaypoints(((150.,-300.),),123)
        data=encode_destinations(12,34,56,78,EXPECTED_SHA256,'level_test_01',saved)
        self.assertEqual(len(data),PACKET_BYTES);self.assertEqual(data[:8],b'SRMMDST1')
        self.assertEqual(struct.unpack_from('<4I3Q',data,8),(1,672,1,12,56,78,34))
        self.assertEqual(data[112:144],bytes.fromhex(EXPECTED_SHA256))
        self.assertEqual(struct.unpack_from('<Q2I',data,144),(123,1,1))
        self.assertEqual(struct.unpack_from('<II2dQ',data,160),(1,1,150.,-300.,0))
        self.assertFalse(any(data[192:]))
        unavailable=encode_destinations(12,34,57,79,EXPECTED_SHA256,'level_test_01',None)
        self.assertEqual(struct.unpack_from('<I',unavailable,16)[0],0)
        self.assertEqual(struct.unpack_from('<Q2I',unavailable,144),(0,1,0));self.assertFalse(any(unavailable[160:]))

    def test_invalid_packet_values_rejected(self):
        for saved in (SavedWaypoints(((math.nan,0),),123),SavedWaypoints(((0,0),)*17,123),SavedWaypoints((),0)):
            with self.assertRaises(ValueError):encode_destinations(12,34,56,78,EXPECTED_SHA256,'level_test_01',saved)

    def test_pose_gate_context_and_transport_freshness(self):
        pose={'positionCandidateXYZ':[1,2,3],'orientationCandidateXYZ':[0,0,1]}
        data=encode_packet(12,34,1,1000,EXPECTED_SHA256,'level_test_01',pose)
        self.assertEqual(pose_context(data,12,34,1100),'level_test_01')
        for pid,session,now in [(13,34,1100),(12,35,1100),(12,34,999),(12,34,1751)]:
            self.assertIsNone(pose_context(data,pid,session,now))
        self.assertIsNone(pose_context(encode_packet(12,34,2,1000,EXPECTED_SHA256,'level_test_01',None),12,34,1100))

    def test_save_refresh_and_deletion(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'CompleteSave1.cfg';path.write_bytes(self.data([self.point()]))
            source=SavedWaypointSource(path)
            self.assertEqual(source.read('level_test_01').points,((150.,-300.),))
            path.write_bytes(self.data([]));self.assertEqual(source.read('level_test_01').points,())
            path.unlink()
            with self.assertRaises(OSError):source.read('level_test_01')

    def test_publisher_lifetime_is_bound_to_fresh_run(self):
        status=dict(schema='srmm-feed-status-v1',pid=12,session=34,state='running',run='a'*32,tick_ms=1000)
        def raw():return json.dumps(status).encode()
        self.assertEqual(feed_run(raw(),12,34,1100),'a'*32)
        self.assertIsNone(feed_run(raw(),12,34,3001))
        self.assertIsNone(feed_run(raw(),12,34,1100,'b'*32))
        self.assertIsNone(feed_run(raw(),13,34,1100))
        status['state']='stopped';self.assertIsNone(feed_run(raw(),12,34,1100))

    def test_saved_objective_resolves_same_map_only(self):
        data=self.objective([{'map':'level_test_01','zones':['ZONE_A'],'cargoState':{'curValue':0,'aimValue':3}},
                             {'map':'level_other_01','zones':['ZONE_B'],'cargoState':{'curValue':0,'aimValue':3}}])
        self.assertEqual(parse_saved_cargo_targets(data,'CompleteSave1'),(('level_test_01','ZONE_A'),('level_other_01','ZONE_B')))
        saved=SavedWaypoints(((0.,0.),),123)
        catalog={'level_test_01':{'ZONE_A':(150.,200.)},'level_other_01':{'ZONE_B':(10.,20.)}}
        combined=add_saved_objective_points(saved,data,'CompleteSave1','level_test_01',catalog)
        self.assertEqual(combined.points,((0.,0.),(150.,200.)));self.assertEqual(combined.kinds,(1,2))
        packet=encode_destinations(12,34,1,1000,EXPECTED_SHA256,'level_test_01',combined)
        self.assertEqual(struct.unpack_from('<I',packet,192)[0],2)

    def test_unsupported_or_finished_objective_does_not_guess_active_stage(self):
        stage={'cargoDeliveryActions':[{'map':'level_test_01','zones':['ZONE_A'],'cargoState':{'curValue':0,'aimValue':3}}]}
        self.assertEqual(parse_saved_cargo_targets(self.objective(stages=[stage,stage]),'CompleteSave1'),())
        stage['truckRepairStates']=[{'unknown':True}]
        self.assertEqual(parse_saved_cargo_targets(self.objective(stages=[stage]),'CompleteSave1'),())
        completed=[{'map':'level_test_01','zones':['ZONE_A'],'cargoState':{'curValue':3,'aimValue':3}}]
        self.assertEqual(parse_saved_cargo_targets(self.objective(completed),'CompleteSave1'),())

    def test_zone_catalog_rejects_ambiguity_and_nonfinite_geometry(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'zones.json'
            entry={'id':'ZONE_A','x':150.,'z':200.,'kind':'static_zone'}
            def save(entries):path.write_text(json.dumps({'schema':'srmm-zone-catalog-v1','maps':{'level_test_01':entries}}))
            save([entry]);self.assertEqual(load_zone_catalog(path)['level_test_01']['ZONE_A'],(150.,200.))
            save([entry,entry])
            with self.assertRaises(ValueError):load_zone_catalog(path)
            entry['x']=float('nan');save([entry])
            with self.assertRaises(ValueError):load_zone_catalog(path)

    @unittest.skipUnless(os.name=='nt','Uses the Windows diagnostic monotonic clock')
    def test_owned_publisher_atomic_output_stop_and_resume(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);save_dir=root/'save';save_dir.mkdir();feed=root/'feed';feed.mkdir()
            save=save_dir/'CompleteSave1.cfg';original=self.data([self.point()]);save.write_bytes(original)
            (feed/'session.local.json').write_text(json.dumps(dict(pid=12,session=34,build=EXPECTED_SHA256,map='level_test_01')))
            status=feed/'status.local.json';stop=feed/'stop.signal';destination=feed/'destinations.local.bin'
            clock=ctypes.WinDLL('kernel32').GetTickCount64;clock.restype=ctypes.c_uint64
            pose={'positionCandidateXYZ':[1,2,3],'orientationCandidateXYZ':[0,0,1]}
            script=Path(__file__).resolve().parents[1]/'tools'/'destination_source.py'
            prior_sequence=0
            for run in ('a'*32,'b'*32):
                if stop.exists():stop.unlink()
                tick=clock()
                (feed/'candidate.local.bin').write_bytes(encode_packet(12,34,1,tick,EXPECTED_SHA256,'level_test_01',pose))
                status.write_text(json.dumps(dict(schema='srmm-feed-status-v1',pid=12,session=34,run=run,state='running',tick_ms=tick)))
                child=subprocess.Popen([sys.executable,str(script),'--save',str(save),'--feed',str(feed),'--pid','12','--session','34',
                    '--seconds','5','--allow-saved-preview','--feed-status',str(status),'--stop-file',str(stop)],
                    stdout=subprocess.PIPE,stderr=subprocess.PIPE,creationflags=subprocess.CREATE_NO_WINDOW)
                try:
                    available=None;deadline=time.monotonic()+3
                    while time.monotonic()<deadline and child.poll() is None:
                        if destination.exists():
                            current=destination.read_bytes()
                            if len(current)==672 and struct.unpack_from('<I',current,16)[0]==1 and struct.unpack_from('<Q',current,24)[0]>prior_sequence:
                                available=current;break
                        time.sleep(.02)
                    self.assertIsNotNone(available,'Publisher did not produce a fresh available snapshot')
                    self.assertEqual(struct.unpack_from('<II2dQ',available,160),(1,1,150.,-300.,0))
                    stop.touch();stdout,stderr=child.communicate(timeout=3)
                    self.assertEqual(child.returncode,0,stderr.decode())
                    final=destination.read_bytes();self.assertEqual(struct.unpack_from('<I',final,16)[0],0)
                    sequence=struct.unpack_from('<Q',final,24)[0];self.assertGreater(sequence,prior_sequence)
                    prior_sequence=sequence
                    self.assertEqual(save.read_bytes(),original)
                    self.assertFalse(list(feed.glob('destinations-next-*.local.tmp')))
                finally:
                    if child.poll() is None:
                        child.terminate();child.communicate(timeout=3)


if __name__=='__main__':unittest.main()
