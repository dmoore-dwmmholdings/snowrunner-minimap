import unittest,tempfile,zipfile,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from map_catalog import discover,MapSelection
class CatalogTests(unittest.TestCase):
 def test_discovery_and_ambiguity(self):
  with tempfile.TemporaryDirectory() as d:
   a=Path(d)/'a.zip';b=Path(d)/'b.zip'
   for p in (a,b):
    with zipfile.ZipFile(p,'w') as z:
     z.writestr('[textures]/pct/level_test_01_map.pct',bytes(100))
     z.writestr('../level_test_02_map.pct',bytes(100))
   result=discover([a]);self.assertEqual(len(result['maps']),1)
   self.assertEqual(result['maps'][0]['status'],'needs_import_and_calibration')
   self.assertEqual(discover([a,b])['maps'][0]['status'],'ambiguous')
 def test_atomic_identity_change(self):
  s=MapSelection();g=s.select('level_test_01');self.assertTrue(s.admit('level_test_01',g,'a',True))
  g2=s.select('level_test_02');self.assertIsNone(s.asset)
  self.assertFalse(s.admit('level_test_01',g,'a',True));self.assertFalse(s.admit('level_test_02',g2,'b'))
  self.assertIsNone(s.asset);self.assertTrue(s.admit('level_test_02',g2,'b',True))
