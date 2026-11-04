import json
import os
from pathlib import Path
import sys
import struct
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import install_manager as installer
import package_minimap as packaging


class SimulatedCrash(BaseException):
    pass


def fixture_dll(value):
    data=bytearray(256);data[:2]=b'MZ';struct.pack_into('<I',data,60,64)
    data[64:68]=b'PE\0\0';struct.pack_into('<H',data,68,0x8664)
    struct.pack_into('<3H',data,84,2,0x2000,0x20b);data[128:]=bytes([value])*128
    return bytes(data)


class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory();self.root=Path(self.temporary.name)
        self.game=self.root/'SnowRunner';self.bin=self.game/'Sources/Bin';self.bin.mkdir(parents=True)
        (self.bin/'SnowRunner.exe').write_bytes(b'MZ'+b'original fixture executable'*5)
        self.sha=installer.digest(self.bin/'SnowRunner.exe')
        self.patches=[patch.object(installer,'GAME_SHA256',self.sha),patch.object(packaging,'GAME_SHA256',self.sha)]
        for p in self.patches:p.start()
        self.build=self.root/'build';self.build.mkdir()
        for i,name in enumerate(installer.DLLS):(self.build/name).write_bytes(fixture_dll(i+1))
        self.package=self.root/'package';packaging.create_package(self.build,self.package,'fixture-1')
        self.state=self.root/'private-state'
        self.closed=lambda:False
    def tearDown(self):
        for p in reversed(self.patches):p.stop()
        self.temporary.cleanup()
    def install(self,**kw):return installer.install(self.game,self.package,self.state,running=self.closed,**kw)
    def uninstall(self,**kw):return installer.uninstall(self.game,self.state,running=self.closed,**kw)
    def snapshot(self):return {str(p.relative_to(self.root)):p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
    def fail(self,_):raise OSError('Owned injected write failure')
    def crash(self,_):raise SimulatedCrash()
    def test_plan_never_writes(self):
        before=self.snapshot();plan=installer.plan_install(self.game,self.package,self.state)
        self.assertEqual(plan['operation'],'install');self.assertEqual(self.snapshot(),before)
    def test_install_uninstall_preserves_user_data(self):
        saves=self.game/'user-save.cfg';saves.write_bytes(b'owned fixture save')
        result=self.install();self.assertEqual(result['status'],'installed')
        for name in installer.DLLS:self.assertEqual((self.bin/name).read_bytes(),(self.package/name).read_bytes())
        (self.state/'private-map.local.dds').write_bytes(b'original fixture asset')
        self.assertEqual(self.uninstall()['status'],'uninstalled')
        for name in installer.DLLS:self.assertFalse((self.bin/name).exists())
        self.assertEqual(saves.read_bytes(),b'owned fixture save')
        self.assertEqual((self.state/'private-map.local.dds').read_bytes(),b'original fixture asset')
    def test_unknown_proxy_refused_without_changes(self):
        (self.bin/'dxgi.dll').write_bytes(b'unknown proxy');before=self.snapshot()
        with self.assertRaisesRegex(ValueError,'Unknown'):self.install()
        self.assertEqual(before,self.snapshot())
    def test_running_game_refuses_even_before_state_creation(self):
        before=self.snapshot()
        with self.assertRaisesRegex(ValueError,'running'):installer.install(self.game,self.package,self.state,running=lambda:True)
        self.assertEqual(before,self.snapshot())
    def test_checksum_tamper_refused(self):
        (self.package/'dxgi.dll').write_bytes(b'MZ'+b'changed'*30)
        with self.assertRaisesRegex(ValueError,'checksum'):self.install()
        self.assertFalse(self.state.exists())
    def test_unsupported_game_refuses_install_but_allows_owned_uninstall(self):
        self.install();(self.bin/'SnowRunner.exe').write_bytes(b'MZupdated game')
        with self.assertRaisesRegex(ValueError,'Unsupported'):self.install()
        self.uninstall();self.assertFalse((self.bin/'dxgi.dll').exists())
    def test_owned_dll_tampering_blocks_uninstall(self):
        self.install();(self.bin/'dxgi.dll').write_bytes(b'changed');before=self.snapshot()
        with self.assertRaisesRegex(ValueError,'changed'):self.uninstall()
        self.assertEqual(before,self.snapshot())
    def test_uninstall_without_receipt_refused(self):
        with self.assertRaisesRegex(ValueError,'No ownership'):self.uninstall()
    def test_install_failure_rolls_back_pair(self):
        with self.assertRaisesRegex(RuntimeError,'restored'):self.install(fault=self.fail)
        for name in installer.DLLS:self.assertFalse((self.bin/name).exists())
        self.assertFalse((self.state/'installed.json').exists());self.assertFalse((self.state/'transaction.json').exists())
    def test_upgrade_failure_restores_previous_bytes_and_receipt(self):
        self.install();old={n:(self.bin/n).read_bytes() for n in installer.DLLS};receipt=(self.state/'installed.json').read_bytes()
        for name in installer.DLLS:(self.build/name).write_bytes(fixture_dll(8))
        self.package=self.root/'package2';packaging.create_package(self.build,self.package,'fixture-2')
        with self.assertRaisesRegex(RuntimeError,'restored'):self.install(fault=self.fail)
        for name in installer.DLLS:self.assertEqual((self.bin/name).read_bytes(),old[name])
        self.assertEqual((self.state/'installed.json').read_bytes(),receipt)
    def test_uninstall_failure_restores_owned_pair(self):
        self.install();old={n:(self.bin/n).read_bytes() for n in installer.DLLS}
        with self.assertRaisesRegex(RuntimeError,'restored'):self.uninstall(fault=self.fail)
        for name in installer.DLLS:self.assertEqual((self.bin/name).read_bytes(),old[name])
    def test_crash_recovery_restores_clean_game(self):
        with self.assertRaises(SimulatedCrash):self.install(fault=self.crash)
        self.assertTrue((self.state/'transaction.json').exists())
        with self.assertRaisesRegex(ValueError,'unfinished'):self.install()
        self.assertEqual(installer.recover(self.game,self.state,running=self.closed)['status'],'recovered')
        for name in installer.DLLS:self.assertFalse((self.bin/name).exists())
    def test_recovery_refuses_external_conflict_without_partial_changes(self):
        with self.assertRaises(SimulatedCrash):self.install(fault=self.crash)
        (self.bin/'dxgi.dll').write_bytes(b'foreign');before=self.snapshot()
        with self.assertRaisesRegex(ValueError,'foreign'):installer.recover(self.game,self.state,running=self.closed)
        self.assertEqual(before,self.snapshot())
    def test_recovery_refuses_changed_receipt(self):
        with self.assertRaises(SimulatedCrash):self.install(fault=self.crash)
        (self.state/'installed.json').write_text('{}');before=self.snapshot()
        with self.assertRaisesRegex(ValueError,'receipt changed'):installer.recover(self.game,self.state,running=self.closed)
        self.assertEqual(before,self.snapshot())
    def test_uninstall_crash_recovers_after_game_update(self):
        self.install()
        with self.assertRaises(SimulatedCrash):self.uninstall(fault=self.crash)
        (self.bin/'SnowRunner.exe').write_bytes(b'MZupdated game')
        installer.recover(self.game,self.state,running=self.closed)
        for name in installer.DLLS:self.assertTrue((self.bin/name).exists())
    def test_game_starts_during_operation_leaves_recovery_pending(self):
        started=[False]
        def fault(_):started[0]=True;raise OSError('started')
        with self.assertRaisesRegex(RuntimeError,'pending'):
            installer.install(self.game,self.package,self.state,running=lambda:started[0],fault=fault)
        self.assertTrue((self.state/'transaction.json').exists())
        with self.assertRaisesRegex(ValueError,'running'):installer.recover(self.game,self.state,running=lambda:True)
        installer.recover(self.game,self.state,running=self.closed)
    def test_state_must_be_outside_game(self):
        with self.assertRaisesRegex(ValueError,'outside'):installer.plan_install(self.game,self.package,self.game/'state')
    def test_package_traversal_and_duplicate_keys_rejected(self):
        manifest=self.package/'package.json';original=json.loads(manifest.read_text())
        original['files']['../evil.dll']='a'*64;manifest.write_text(json.dumps(original))
        with self.assertRaisesRegex(ValueError,'schema'):installer.package_files(self.package)
        manifest.write_text('{"schema":"x","schema":"y"}')
        with self.assertRaisesRegex(ValueError,'Duplicate'):installer.package_files(self.package)
    def test_package_does_not_copy_extra_files_or_overwrite_existing(self):
        (self.build/'secret.local.json').write_text('private fixture')
        output=self.root/'other-package';packaging.create_package(self.build,output,'fixture-3')
        self.assertEqual({p.name for p in output.iterdir()},set(installer.DLLS)|{'package.json'})
        with self.assertRaises(FileExistsError):packaging.create_package(self.build,output,'fixture-4')
    def test_wrong_architecture_is_rejected_even_with_matching_hash(self):
        data=bytearray(fixture_dll(1));struct.pack_into('<H',data,68,0x14c)
        path=self.package/'dxgi.dll';path.write_bytes(data)
        manifest=json.loads((self.package/'package.json').read_text());manifest['files']['dxgi.dll']=installer.digest(path)
        (self.package/'package.json').write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError,'x64'):installer.package_files(self.package)
    def test_recovery_removes_verified_staged_file(self):
        with self.assertRaises(SimulatedCrash):self.install(fault=self.crash)
        journal=json.loads((self.state/'transaction.json').read_text())
        stage=self.bin/('.srmm-install-'+journal['id']+'-dxgi.dll.tmp')
        stage.write_bytes((self.package/'dxgi.dll').read_bytes())
        installer.recover(self.game,self.state,running=self.closed)
        self.assertFalse(stage.exists())
    def test_symlink_refused_when_supported(self):
        link=self.root/'linked-package'
        try:link.symlink_to(self.package,target_is_directory=True)
        except OSError:self.skipTest('Symlink privilege unavailable; junctions rejected by same path validation.')
        with self.assertRaisesRegex(ValueError,'links'):installer.package_files(link)
    def test_steam_metadata_discovery_multiple_libraries(self):
        steam=self.root/'Steam';(steam/'steamapps').mkdir(parents=True)
        lib=self.root/'Other Library';(lib/'steamapps/common/Game/Sources/Bin').mkdir(parents=True)
        (lib/'steamapps/common/Game/Sources/Bin/SnowRunner.exe').write_bytes(b'fixture')
        raw=str(lib).replace('\\','\\\\')
        (steam/'steamapps/libraryfolders.vdf').write_text('"libraryfolders" { "1" { "path" "'+raw+'" } }')
        (lib/'steamapps/appmanifest_1465360.acf').write_text('"AppState" { "installdir" "Game" }')
        self.assertEqual(installer.steam_candidates(steam),[str(lib/'steamapps/common/Game')])

    def legacy_pair(self):
        for name in installer.DLLS:(self.bin/name).write_bytes((self.package/name).read_bytes())
        hashes={name:installer.digest(self.bin/name) for name in installer.DLLS}
        p=patch.object(installer,'LEGACY_BUILDS',{'fixture-legacy':hashes});p.start();self.patches.append(p)
        return hashes

    def test_legacy_plan_does_not_write_or_silently_enable_install(self):
        hashes=self.legacy_pair();before=self.snapshot()
        plan=installer.plan_adopt_legacy(self.game,self.state)
        self.assertEqual(plan['operation'],'adopt_legacy');self.assertEqual(plan['files'],hashes)
        self.assertEqual(self.snapshot(),before)
        with self.assertRaisesRegex(ValueError,'Unknown'):self.install()

    def test_legacy_adoption_backups_pair_and_enables_owned_upgrade(self):
        hashes=self.legacy_pair();original={n:(self.bin/n).read_bytes() for n in installer.DLLS}
        receipt=installer.adopt_legacy(self.game,self.state,running=self.closed)
        self.assertTrue(receipt['adopted_legacy']);self.assertEqual(receipt['files'],hashes)
        for name in installer.DLLS:
            self.assertEqual((self.bin/name).read_bytes(),original[name])
            self.assertEqual((Path(receipt['legacy_backup'])/name).read_bytes(),original[name])
        self.assertFalse((self.state/'transaction.json').exists())
        self.assertEqual(installer.plan_install(self.game,self.package,self.state)['operation'],'upgrade')
        self.install();self.uninstall()
        for name in installer.DLLS:self.assertFalse((self.bin/name).exists())

    def test_legacy_unknown_partial_and_modified_pairs_refused(self):
        self.legacy_pair()
        for name in installer.DLLS:
            original=(self.bin/name).read_bytes();(self.bin/name).unlink()
            with self.assertRaisesRegex(ValueError,'exact known'):installer.plan_adopt_legacy(self.game,self.state)
            (self.bin/name).write_bytes(original+b'changed')
            with self.assertRaisesRegex(ValueError,'exact known'):installer.adopt_legacy(self.game,self.state,running=self.closed)
            (self.bin/name).write_bytes(original)
        self.assertFalse(self.state.exists())

    def test_legacy_pending_or_existing_receipt_refused(self):
        self.legacy_pair();self.state.mkdir()
        (self.state/'transaction.json').write_text('{}');before=self.snapshot()
        with self.assertRaisesRegex(ValueError,'unfinished'):installer.adopt_legacy(self.game,self.state,running=self.closed)
        self.assertEqual(before,self.snapshot());(self.state/'transaction.json').unlink()
        (self.state/'installed.json').write_text('{}');before=self.snapshot()
        with self.assertRaisesRegex(ValueError,'already exists'):installer.adopt_legacy(self.game,self.state,running=self.closed)
        self.assertEqual(before,self.snapshot())

    def test_legacy_running_or_wrong_game_build_refused(self):
        self.legacy_pair();before=self.snapshot()
        with self.assertRaisesRegex(ValueError,'running'):installer.adopt_legacy(self.game,self.state,running=lambda:True)
        self.assertEqual(before,self.snapshot())
        (self.bin/'SnowRunner.exe').write_bytes(b'game update')
        with self.assertRaisesRegex(ValueError,'Unsupported'):installer.adopt_legacy(self.game,self.state,running=self.closed)
        self.assertFalse(self.state.exists())

    def test_legacy_crash_after_ownership_recovers_without_touching_dlls(self):
        self.legacy_pair();original={n:(self.bin/n).read_bytes() for n in installer.DLLS}
        def crash_after_receipt(phase):
            if phase=='receipt_written':raise SimulatedCrash()
        with self.assertRaises(SimulatedCrash):installer.adopt_legacy(self.game,self.state,running=self.closed,fault=crash_after_receipt)
        self.assertTrue((self.state/'installed.json').exists())
        installer.recover(self.game,self.state,running=self.closed)
        self.assertFalse((self.state/'installed.json').exists())
        for name in installer.DLLS:self.assertEqual((self.bin/name).read_bytes(),original[name])

    def test_legacy_concurrent_game_start_preserves_pair_pending_recovery(self):
        self.legacy_pair();started=[False];original={n:(self.bin/n).read_bytes() for n in installer.DLLS}
        def start_game(_):started[0]=True
        with self.assertRaisesRegex(RuntimeError,'recovery is pending'):
            installer.adopt_legacy(self.game,self.state,running=lambda:started[0],fault=start_game)
        self.assertFalse((self.state/'installed.json').exists())
        for name in installer.DLLS:self.assertEqual((self.bin/name).read_bytes(),original[name])
        installer.recover(self.game,self.state,running=self.closed)

    def test_legacy_backup_failure_never_grants_ownership(self):
        self.legacy_pair();original={n:(self.bin/n).read_bytes() for n in installer.DLLS}
        with patch.object(installer,'checked_copy',side_effect=OSError('fixture backup error')):
            with self.assertRaisesRegex(OSError,'backup'):installer.adopt_legacy(self.game,self.state,running=self.closed)
        self.assertFalse((self.state/'installed.json').exists())
        for name in installer.DLLS:self.assertEqual((self.bin/name).read_bytes(),original[name])

    def test_legacy_pair_change_during_backup_does_not_grant_ownership(self):
        self.legacy_pair()
        def change_pair(_):(self.bin/'dxgi.dll').write_bytes(b'foreign replacement')
        with self.assertRaisesRegex(RuntimeError,'pending'):
            installer.adopt_legacy(self.game,self.state,running=self.closed,fault=change_pair)
        self.assertFalse((self.state/'installed.json').exists())
        self.assertEqual((self.bin/'dxgi.dll').read_bytes(),b'foreign replacement')

    def test_legacy_competing_installer_lock_refuses_adoption(self):
        self.legacy_pair()
        with installer.transaction_lock(self.state):
            with self.assertRaises(OSError):installer.adopt_legacy(self.game,self.state,running=self.closed)
        self.assertFalse((self.state/'installed.json').exists())
        self.assertFalse((self.state/'transaction.json').exists())


if __name__=='__main__':unittest.main()
