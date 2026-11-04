import sys
import tempfile
import unittest
import ctypes
from ctypes import wintypes
import subprocess
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from feed_lifecycle import atomic_json, read_json, stop_requested, request_stop, run_samples, ProcessLifetime
from candidate_feed import resume_sequence, encode_packet, EXPECTED_SHA256


class FeedLifecycleTests(unittest.TestCase):
    @unittest.skipUnless(sys.platform=='win32','Windows process handles')
    def test_owned_process_exit_is_detected_by_retained_handle(self):
        kernel=ctypes.WinDLL('kernel32',use_last_error=True)
        kernel.OpenProcess.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.DWORD]
        kernel.OpenProcess.restype=wintypes.HANDLE
        kernel.CloseHandle.argtypes=[wintypes.HANDLE]
        child=subprocess.Popen([sys.executable,'-c','import sys; sys.stdin.read(1)'],stdin=subprocess.PIPE,
                               stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        handle=None
        try:
            handle=kernel.OpenProcess(0x1000,False,child.pid)
            self.assertTrue(handle)
            lifetime=ProcessLifetime(handle)
            self.assertTrue(lifetime.alive())
            child.communicate(b'x',timeout=5)
            self.assertFalse(lifetime.alive())
        finally:
            if handle:kernel.CloseHandle(handle)
            if child.poll() is None:
                child.kill();child.wait(timeout=5)

    def test_game_exit_revokes_even_after_a_good_sample(self):
        alive = iter((True, False)); published=[]; updates=[]
        result = run_samples(lambda: ('level_test_01', {'pose': 1}, 10),
                             lambda *v: published.append(v), lambda: next(alive),
                             lambda: False, 10, pause=lambda _: None,
                             update=lambda *v: updates.append(v))
        self.assertEqual(result, 'game_exited')
        self.assertEqual(published[-1], ('level_test_01', None, None))
        self.assertEqual(updates[-1], ('game_exited', 'level_test_01', False))

    def test_stop_before_first_sample_still_revokes(self):
        values=[]
        result=run_samples(lambda: self.fail('must not sample'), lambda *v: values.append(v),
                           lambda: True, lambda: True, 10)
        self.assertEqual(result,'stop_requested'); self.assertEqual(values, [(None,None,None)])

    def test_exception_revokes_and_reports_error(self):
        values=[]; updates=[]
        def fail(): raise RuntimeError('owned fixture')
        with self.assertRaisesRegex(RuntimeError, 'owned fixture'):
            run_samples(fail,lambda *v: values.append(v),lambda: True,lambda: False,10,
                        update=lambda *v: updates.append(v))
        self.assertEqual(values,[(None,None,None)]);self.assertEqual(updates[-1][0],'error')

    def test_deadline_is_independent_of_sample_availability(self):
        now=[0.]; values=[]
        def pause(seconds):now[0]+=seconds
        result=run_samples(lambda: (None,None,1), lambda *v: values.append(v),lambda: True,
                           lambda: False,.1,clock=lambda:now[0],pause=pause)
        self.assertEqual(result,'duration_limit');self.assertGreaterEqual(len(values),2)
        self.assertIsNone(values[-1][1])

    def test_stop_is_exact_run_identity_not_a_stale_file(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);status={'schema':'srmm-feed-status-v1','pid':12,'session':34,'run':'a'*32,'state':'running'}
            request_stop(root,status)
            self.assertTrue(stop_requested(root/'stop.local.json',12,34,'a'*32))
            for pid,session,run in ((13,34,'a'*32),(12,35,'a'*32),(12,34,'b'*32)):
                self.assertFalse(stop_requested(root/'stop.local.json',pid,session,run))
            atomic_json(root/'status.local.json',status)
            self.assertEqual(read_json(root/'status.local.json'),status)
            self.assertEqual(len(list(root.iterdir())),2)

    def test_resume_allows_same_session_map_transition_only_explicitly(self):
        packet=encode_packet(12,34,100,1000,EXPECTED_SHA256,'level_test_02',None)
        with self.assertRaises(ValueError):resume_sequence(packet,12,34,'level_test_01')
        self.assertEqual(resume_sequence(packet,12,34,'level_test_01',allow_map_change=True),100)
        with self.assertRaises(ValueError):resume_sequence(packet,13,34,'level_test_01',allow_map_change=True)


if __name__=='__main__':unittest.main()
