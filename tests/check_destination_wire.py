"""Cross-language saved-waypoint wire regression using only original fixtures."""
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from destination_source import SavedWaypoints,encode_destinations
from probe_live_pose import EXPECTED_SHA256


def main():
    if len(sys.argv)!=2:raise SystemExit('Pass the owned destination host executable.')
    host=Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='srmm-destination-wire-') as folder:
        path=Path(folder)/'fixture.bin'
        for source,count in ((None,0),(SavedWaypoints((),1700000000000),0),
                             (SavedWaypoints(((12.5,-22.25),),1700000000000),1),
                             (SavedWaypoints(((12.5,-22.25),(100.,200.)),1700000000000,kinds=(1,2)),2),
                             (SavedWaypoints(tuple((float(i),-float(i)) for i in range(16)),1700000000000),16)):
            packet=encode_destinations(42,99,1,1000,EXPECTED_SHA256,'level_test_01',source)
            path.write_bytes(packet)
            result=subprocess.run([str(host),'--packet',str(path)],capture_output=True,text=True,check=True)
            if f'count={count}' not in result.stdout:raise AssertionError(result.stdout)
        malformed=bytearray(packet);malformed[-1]=1;path.write_bytes(malformed)
        result=subprocess.run([str(host),'--packet',str(path)],capture_output=True,text=True)
        if result.returncode==0:raise AssertionError('Native parser accepted nonzero reserved bytes.')
    print('PASS 6 Python-to-native saved-waypoint wire checks; original data only.')


if __name__=='__main__':main()
