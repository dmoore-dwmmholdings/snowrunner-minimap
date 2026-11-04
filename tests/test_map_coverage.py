import io
from contextlib import redirect_stdout
from pathlib import Path
import tempfile
import sys
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from map_catalog import discover_installed
from import_map_registry import import_registry
from test_map_import import fixture_archives


def zip_entries(path,entries):
    with zipfile.ZipFile(path,'w') as archive:
        for name,data in entries:archive.writestr(name,data)


class CoverageTests(unittest.TestCase):
    def test_main_menu_container_adds_trial_texture_without_false_terrain_maps(self):
        with tempfile.TemporaryDirectory() as folder,redirect_stdout(io.StringIO()):
            client=fixture_archives(folder)
            (client/'shared_textures.pak').rename(client/'main_menu_textures.pak')
            with zipfile.ZipFile(client/'level_fixture_01.pak','a') as archive:
                archive.writestr('[textures]/pct/level_fixture_01_height_map.pct',bytes(100))
            report=discover_installed(client)
            self.assertEqual(report['ready_count'],1)
            self.assertEqual(report['candidate_count'],1)
            _,registry=import_registry(client,Path(folder)/'cache')
            self.assertEqual(len(registry['maps']),1)
            self.assertTrue(registry['coverage']['all_installed_candidates_imported'])

    def test_missing_texture_is_reported_instead_of_omitted(self):
        with tempfile.TemporaryDirectory() as folder,redirect_stdout(io.StringIO()):
            client=fixture_archives(folder)
            zip_entries(client/'level_missing_01.pak',[])
            _,registry=import_registry(client,Path(folder)/'cache')
            self.assertEqual(registry['coverage']['candidate_count'],2)
            self.assertEqual(registry['unavailable'][0]['reason_code'],'missing_texture')
            self.assertFalse(registry['coverage']['all_installed_candidates_imported'])

    def test_future_shared_container_and_level_local_texture_are_discovered(self):
        for location in ('shared_textures_dlc_future.pak','level_fixture_01.pak'):
            with self.subTest(location=location),tempfile.TemporaryDirectory() as folder:
                client=fixture_archives(folder)
                source=client/'shared_textures.pak'
                with zipfile.ZipFile(source) as archive:
                    entry=archive.infolist()[0];payload=archive.read(entry)
                if location=='level_fixture_01.pak':
                    with zipfile.ZipFile(client/location,'a') as archive:archive.writestr(entry.filename,payload)
                    source.unlink()
                else:source.rename(client/location)
                self.assertEqual(discover_installed(client)['ready_count'],1)

    def test_duplicate_texture_never_assumes_archive_override_order(self):
        with tempfile.TemporaryDirectory() as folder:
            client=fixture_archives(folder,duplicate=True)
            report=discover_installed(client)
            self.assertEqual(report['ready_count'],0)
            self.assertEqual(report['maps'][0]['status'],'ambiguous_texture')

    def test_unreadable_archive_prevents_complete_coverage_claim(self):
        with tempfile.TemporaryDirectory() as folder:
            client=fixture_archives(folder)
            (client/'shared_textures_broken.pak').write_bytes(b'original bad zip fixture')
            report=discover_installed(client)
            self.assertFalse(report['inventory_complete'])
            self.assertEqual(report['maps'][0]['status'],'incomplete_inventory')

    def test_main_menu_scene_explicitly_excluded_but_trials_are_not(self):
        with tempfile.TemporaryDirectory() as folder:
            client=fixture_archives(folder)
            zip_entries(client/'level_main_menu_fixture.pak',[])
            zip_entries(client/'level_trial_fixture.pak',[])
            report=discover_installed(client)
            self.assertEqual(report['level_archive_count'],3)
            self.assertEqual(report['candidate_count'],2)
            self.assertEqual(report['excluded'][0]['reason_code'],'non_gameplay_scene')
            self.assertEqual(report['maps'][1]['map'],'level_trial_fixture')

    def test_invalid_texture_size_reports_unsupported_instead_of_missing(self):
        with tempfile.TemporaryDirectory() as folder:
            client=fixture_archives(folder)
            zip_entries(client/'shared_textures.pak',[('[textures]/pct/level_fixture_01_map.pct',bytes(2))])
            report=discover_installed(client)
            self.assertEqual(report['maps'][0]['status'],'unsupported_texture_entry')

    def test_subset_cannot_claim_all_map_coverage(self):
        with tempfile.TemporaryDirectory() as folder,redirect_stdout(io.StringIO()):
            client=fixture_archives(folder)
            _,report=import_registry(client,Path(folder)/'cache',['level_fixture_01'])
            self.assertEqual(report['coverage']['selection'],'explicit_subset')
            self.assertFalse(report['coverage']['all_installed_candidates_imported'])

    def test_bounds_refuse_truncated_inventory_or_unloadable_registry(self):
        with tempfile.TemporaryDirectory() as folder:
            client=fixture_archives(folder)
            with patch('map_catalog.MAX_ARCHIVES',1):
                with self.assertRaisesRegex(ValueError,'inventory exceeds'):discover_installed(client)
            with patch('map_catalog.MAX_MEMBERS',1):
                self.assertFalse(discover_installed(client)['inventory_complete'])
            report=discover_installed(client);report['candidate_count']=129
            output=Path(folder)/'cache'
            with patch('import_map_registry.discover_installed',return_value=report):
                with self.assertRaisesRegex(ValueError,'registry limit'):import_registry(client,output)
            self.assertFalse(output.exists())


if __name__=='__main__':unittest.main()
