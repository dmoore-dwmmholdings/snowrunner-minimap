import json,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from setup_minimap import import_local_maps

class SetupTests(unittest.TestCase):
    def test_unsupported_zones_keep_waypoint_only_startup_available(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);registry={'maps':[{'map':'level_test_01'}],'unavailable':[],'coverage':{'candidate_count':1}}
            with patch('setup_minimap.import_registry',return_value=(root/'maps.json',registry)),patch('setup_minimap.read_zones',side_effect=ValueError('unknown zone format')):
                path,zones,result=import_local_maps(root/'game',root)
            self.assertIsNone(zones)
            self.assertEqual(result['zone_support'],{'supported_maps':0,'unavailable_maps':1})
            self.assertEqual(len(json.loads((root/'zones.local.json').read_text())['unavailable']),1)
    def test_zone_catalog_returned_only_when_usable(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);registry={'maps':[{'map':'level_test_01'}],'unavailable':[],'coverage':{'candidate_count':1}}
            with patch('setup_minimap.import_registry',return_value=(root/'maps.json',registry)),patch('setup_minimap.read_zones',return_value=[]):
                _,zones,result=import_local_maps(root/'game',root)
            self.assertTrue(zones.is_file());self.assertEqual(result['zone_support']['supported_maps'],1)

if __name__=='__main__':unittest.main()
