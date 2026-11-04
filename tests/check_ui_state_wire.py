"""Cross-language owned wire fixture. Does not inspect a game or provide a profile."""
from pathlib import Path
import subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from ui_state_feed import encode_ui_state,MENU
with tempfile.TemporaryDirectory() as folder:
    path=Path(folder)/'ui-fixture.bin';path.write_bytes(encode_ui_state(42,99,5,1000,MENU,'ab'*32))
    subprocess.run([sys.argv[1],str(path)],check=True)
