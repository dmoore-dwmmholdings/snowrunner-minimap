from contextlib import redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from import_map_registry import import_registry


def container(fields):
    data=b''
    for key,value in fields:data+=struct.pack('<HI',key,len(data)+6+len(value))+value
    return data


def fixture_archives(folder, image_width=48, image_height=96, duplicate=False):
    client=Path(folder)/'game'/'preload'/'paks'/'client';client.mkdir(parents=True)
    level='level_fixture_01'
    raw=bytes(image_width*image_height)
    pct=container([(0xf0,b'TCIP'),(0x102,struct.pack('<4I',image_width,image_height,1,1)),
                   (0xf2,struct.pack('<I',15)),(0xf9,struct.pack('<I',1)),
                   (0x107,struct.pack('<2I',0,len(raw))),(0x104,bytes(4)),(0xff,raw),(1,b'')])
    for name in ('shared_textures.pak','shared_textures_base.pak') if duplicate else ('shared_textures.pak',):
        with zipfile.ZipFile(client/name,'w') as archive:archive.writestr(f'[textures]/pct/{level}_map.pct',pct)
    terrain=container([(0xf0,b'TCIP'),(0x102,struct.pack('<4I',49,97,1,1)),
                       (0xf2,struct.pack('<I',0x37)),(0xf9,struct.pack('<I',1)),
                       (0x107,struct.pack('<2I',0,49*97*2)),(0x104,bytes(4)),(1,b'')])
    with zipfile.ZipFile(client/(level+'.pak'),'w') as archive:
        archive.writestr(f'[textures]/pct/{level}_height_map.pct_header',terrain)
        archive.writestr('[scene]/data_for_ssl.bin',bytes(32)+struct.pack('<4f2I',24,48,-8,-20,1,2)+bytes(8))
    return client


class MapImportTests(unittest.TestCase):
    def test_full_original_fixture_import_and_existing_output_preserved(self):
        with tempfile.TemporaryDirectory() as folder,redirect_stdout(io.StringIO()):
            client=fixture_archives(folder);output=Path(folder)/'private'
            path,data=import_registry(client,output)
            self.assertEqual(len(data['maps']),1);self.assertEqual(data['unavailable'],[])
            entry=data['maps'][0];asset=Path(entry['map_path'])
            self.assertEqual(entry['map_sha256'],hashlib.sha256(asset.read_bytes()).hexdigest())
            self.assertEqual(entry['uv_per_pixel_v'],1/48)
            self.assertEqual(entry['affine'][2],8/24)
            self.assertEqual(entry['geometry'],'alignment_preview')
            original=path.read_bytes()
            with self.assertRaises(ValueError):import_registry(client,output)
            self.assertEqual(path.read_bytes(),original)

    def test_cropped_or_resized_image_is_not_admitted(self):
        with tempfile.TemporaryDirectory() as folder,redirect_stdout(io.StringIO()):
            client=fixture_archives(folder,image_height=48);output=Path(folder)/'private'
            _,data=import_registry(client,output)
            self.assertEqual(data['maps'],[]);self.assertEqual(len(data['unavailable']),1)
            self.assertEqual(list(output.glob('*.dds')),[])

    def test_duplicate_texture_source_is_not_admitted(self):
        with tempfile.TemporaryDirectory() as folder,redirect_stdout(io.StringIO()):
            client=fixture_archives(folder,duplicate=True)
            _,data=import_registry(client,Path(folder)/'private')
            self.assertEqual(data['maps'],[])
            self.assertIn('Ambiguous',data['unavailable'][0]['reason'])

    def test_installation_output_and_unknown_map_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            client=fixture_archives(folder)
            with self.assertRaises(ValueError):import_registry(client,client/'cache')
            with self.assertRaises(ValueError):import_registry(client,Path(folder)/'private',['level_unknown_01'])
            self.assertFalse((client/'cache').exists())

    def test_invalid_level_archive_is_reported_without_asset(self):
        with tempfile.TemporaryDirectory() as folder,redirect_stdout(io.StringIO()):
            client=fixture_archives(folder)
            (client/'level_fixture_01.pak').write_bytes(b'original invalid archive fixture')
            output=Path(folder)/'private'
            _,data=import_registry(client,output)
            self.assertEqual(data['maps'],[])
            self.assertEqual(len(data['unavailable']),1)
            self.assertEqual(list(output.glob('*.dds')),[])


if __name__=='__main__':unittest.main()
