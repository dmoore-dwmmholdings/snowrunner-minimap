"""Cross-language fixture only: Python encoder -> original native decoder."""
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from candidate_feed import encode_packet

host=Path(sys.argv[1]).resolve(strict=True)
with tempfile.TemporaryDirectory(prefix='srmm-wire-') as directory:
    packet=Path(directory)/'original-fixture.bin'
    packet.write_bytes(encode_packet(42,2**63+99,17,123456789,'ab'*32,'level_test_01',{'positionCandidateXYZ':(100,0,200),'orientationCandidateXYZ':(0,0,1)}))
    result=subprocess.run([str(host),'--decode',str(packet)],capture_output=True,text=True,timeout=10,check=True)
    assert result.stdout.strip()==f'42 {2**63+99} 17 123456789 level_test_01 100 200 1',result.stdout
    packet.write_bytes(packet.read_bytes()+b'x')
    result=subprocess.run([str(host),'--decode',str(packet)],capture_output=True,text=True,timeout=10)
    assert result.returncode==1,result.returncode
print('PASS: Python/native packet compatibility and oversized-record rejection; original fixture only.')
