import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import ui_state_feed as ui


class UiStateTests(unittest.TestCase):
    def profile(self):
        return {'schema':'srmm-verified-ui-profile-v1','verified':True,'executableSha256':ui.EXPECTED_SHA256,
            'validation':{'gameplay_returns':2,'observed_states':['gameplay','pause','settings','full_map','gameplay_return']},
            'predicates':[{'rva':4112,'width':1,'gameplay':[0],'menu':[1,2]}],
            'anchors':[{'rootRva':4096,'type':'.?AVOWNED_FIXTURE@@'}]}

    def test_wire_contract(self):
        data=ui.encode_ui_state(42,99,5,1000,ui.MENU,'ab'*32)
        self.assertEqual(len(data),128)
        self.assertEqual(struct.unpack_from('<8s4I3Q',data),(b'SRMMUI01',1,128,2,42,5,1000,99))
        self.assertEqual(data[48:80],bytes.fromhex('ab'*32));self.assertEqual(data[80:],bytes(48))
        for state in (-1,3,True):
            with self.assertRaises(ValueError):ui.encode_ui_state(42,99,5,1000,state)
        for pid,session,seq,tick in ((0,99,5,1000),(42,0,5,1000),(42,99,0,1000),(42,99,5,-1)):
            with self.assertRaises(ValueError):ui.encode_ui_state(pid,session,seq,tick,ui.UNKNOWN)

    def test_profile_requires_explicit_verification_and_bound(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'profile.json'
            def load(profile):path.write_text(json.dumps(profile));return ui.load_profile(path,{'.data':(4096,64)})
            self.assertEqual(load(self.profile())['predicates'][0]['rva'],4112)
            for changed in ('verified','fingerprint','history','range','overlap','width'):
                profile=self.profile()
                if changed=='verified':profile['verified']=False
                elif changed=='fingerprint':profile['executableSha256']='ab'*32
                elif changed=='history':profile['validation']['gameplay_returns']=1
                elif changed=='range':profile['predicates'][0]['rva']=4160
                elif changed=='overlap':profile['predicates'][0]['menu']=[0]
                elif changed=='width':profile['predicates'][0]['width']=8
                with self.assertRaises(ValueError,msg=changed):load(profile)

    def test_profile_rejects_malformed_container_types(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'profile.json'
            cases=[[],None,42]
            for key,bad in [('validation',[]),('predicates',[None]),('anchors',[False])]:
                profile=self.profile();profile[key]=bad;cases.append(profile)
            for profile in cases:
                path.write_text(json.dumps(profile))
                with self.assertRaises(ValueError):ui.load_profile(path,{'.data':(4096,64)})

    def test_unknown_values_never_infer_gameplay(self):
        profile=self.profile()
        self.assertEqual(ui.classify_values(profile,[0]),ui.GAMEPLAY)
        self.assertEqual(ui.classify_values(profile,[2]),ui.MENU)
        for values in ([3],[None],[]):self.assertEqual(ui.classify_values(profile,values),ui.UNKNOWN)
        profile['predicates'].append({'rva':4113,'width':1,'gameplay':[0],'menu':[1]})
        self.assertEqual(ui.classify_values(profile,[0,1]),ui.UNKNOWN)

    def test_transitional_reads_and_changed_roots_fail_unknown(self):
        class Reader:
            def __init__(self,values):self.values=iter(values)
            def read(self,address,size):return next(self.values)
        profile=self.profile()
        with patch.object(ui,'pointer',side_effect=[5000,5000]),patch.object(ui,'rtti_name',return_value='.?AVOWNED_FIXTURE@@'):
            self.assertEqual(ui.sample_ui(Reader([b'\x01',b'\x01']),100,10000,profile),ui.MENU)
        with patch.object(ui,'pointer',return_value=5000),patch.object(ui,'rtti_name',return_value='.?AVOWNED_FIXTURE@@'):
            self.assertEqual(ui.sample_ui(Reader([b'\x01',b'\x00']),100,10000,profile),ui.UNKNOWN)
        with patch.object(ui,'pointer',side_effect=[5000,5008]),patch.object(ui,'rtti_name',return_value='.?AVOWNED_FIXTURE@@'):
            self.assertEqual(ui.sample_ui(Reader([b'\x00',b'\x00']),100,10000,profile),ui.UNKNOWN)
        with patch.object(ui,'pointer',return_value=5000),patch.object(ui,'rtti_name',return_value='.?AVWRONG_TYPE@@'):
            self.assertEqual(ui.sample_ui(Reader([]),100,10000,profile),ui.UNKNOWN)


if __name__=='__main__':unittest.main()
