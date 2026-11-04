import sys,tempfile,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from minimap_settings import update_display
class SettingsTests(unittest.TestCase):
 def test_preserve_session_and_apply(self):
  with tempfile.TemporaryDirectory() as d:
   p=Path(d)/'session.ini';p.write_text('version=1\nenabled=1\nsession=42\nmap=level_test_01\nviewport=0 0 320\n')
   update_display(p,2,'motion','square',20,30,360)
   self.assertIn('session=42\n',p.read_text());self.assertIn('zoom=2.0\n',p.read_text())
   before=p.read_bytes()
   for invalid in ('nan','inf','0','9'):
    with self.assertRaises(ValueError):update_display(p,invalid,'motion','square',20,30,360)
    self.assertEqual(p.read_bytes(),before)
 def test_collision_preserves_unowned_temp(self):
  with tempfile.TemporaryDirectory() as d:
   p=Path(d)/'session.ini';p.write_text('version=1\nenabled=1\n')
   t=p.with_name('settings-next.local.ini');t.write_text('other writer')
   with self.assertRaises(FileExistsError):update_display(p,1,'north','circle',0,0,320)
   self.assertEqual(t.read_text(),'other writer')
