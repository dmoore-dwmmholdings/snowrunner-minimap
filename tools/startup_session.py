"""Start one explicit, fingerprinted installation without hand-written PID/layout files."""
import argparse, ctypes, csv, io, json, os, secrets, subprocess, sys, time
from pathlib import Path
from contextlib import contextmanager
from install_manager import digest, package_files, GAME_SHA256, atomic_json, plain_path
from map_switcher import load_registry, read_fields
from session_config import config_lock

def supported_layout():
    # Original observed root RVAs; never absolute addresses or captured game bytes.
    # This adapter remains restricted to one exact executable fingerprint.
    return {'schema':'srmm-live-candidates-v1','executableSha256':GAME_SHA256,
            'discovery':{'completeWithinBound':True,
                         'truckCandidates':[{'rootRva':44645368}],
                         'levelCandidates':[{'rootRva':44405056}]}}

def verify_install(game_root, package):
    root=plain_path(game_root);exe=root/'Sources/Bin/SnowRunner.exe'
    if digest(exe)!=GAME_SHA256:raise ValueError('Unsupported SnowRunner build. Keep the minimap disabled until an adapter update is available.')
    _,info=package_files(package)
    for name,expected in info['files'].items():
        if digest(exe.parent/name)!=expected:raise ValueError('Minimap installation differs from this package. Close the game and use Install / repair.')
    return exe.resolve()

def running_install(exe):
    """Enumerate by name, then match the full executable path with query-only handles."""
    result=subprocess.run(['tasklist','/FI','IMAGENAME eq SnowRunner.exe','/FO','CSV','/NH'],capture_output=True,text=True,check=True,timeout=10,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    k=ctypes.WinDLL('kernel32',use_last_error=True)
    k.OpenProcess.argtypes=[ctypes.c_uint32,ctypes.c_int,ctypes.c_uint32];k.OpenProcess.restype=ctypes.c_void_p
    k.CloseHandle.argtypes=[ctypes.c_void_p]
    k.QueryFullProcessImageNameW.argtypes=[ctypes.c_void_p,ctypes.c_uint32,ctypes.c_wchar_p,ctypes.POINTER(ctypes.c_uint32)]
    matches=[]
    for row in csv.reader(io.StringIO(result.stdout)):
        if len(row)<2 or row[0].casefold()!='snowrunner.exe':continue
        pid=int(row[1]);handle=k.OpenProcess(0x1000,False,pid)
        if not handle:raise OSError('Cannot verify a running SnowRunner process. Run game and minimap as the same Windows user.')
        try:
            buf=ctypes.create_unicode_buffer(32768);size=ctypes.c_uint32(len(buf))
            if not k.QueryFullProcessImageNameW(handle,0,buf,ctypes.byref(size)):raise OSError('Cannot verify the game executable path.')
            if Path(buf.value).resolve()==exe:matches.append(pid)
            else:raise ValueError('Another SnowRunner installation is running. Close it before starting this installation.')
        finally:k.CloseHandle(handle)
    if len(matches)>1:raise ValueError('More than one matching game process is running.')
    return matches[0] if matches else None

@contextmanager
def starter_lock(data):
    path=data/'starter.local.ini'
    with config_lock(path):yield

def session_fields(registry, old, session, feed):
    level=next(iter(registry))
    fields={'version':'1','enabled':'1',**registry[level], 'session':str(session),
            'packet_path':str(feed/'candidate.local.bin'),'destination_path':str(feed/'destinations.local.bin'),
            'viewport':'50 50 360','tracking':'motion','shape':'circle','zoom':'1'}
    for key in ('viewport','tracking','shape','zoom'):
        if key in old:fields[key]=old[key]
    return fields

def prepare_session(data, registry_path, pid, save=None, zones=None, record=False):
    if type(pid) is not int or not 0<pid<2**32:raise ValueError('Invalid game process identity.')
    if type(record) is not bool:raise ValueError('Recording selection must be boolean.')
    if zones and not save:raise ValueError('Static destination zones require an explicitly selected save.')
    if save:
        from destination_source import SavedWaypointSource,load_zone_catalog
        SavedWaypointSource(save)
        if not Path(save).is_file():raise ValueError('Selected save does not exist.')
        if zones:load_zone_catalog(zones)
    registry=load_registry(Path(registry_path))
    if not registry:raise ValueError('No supported maps. Run Import installed maps first.')
    data=Path(data).resolve();config=data/'session.local.ini'
    run=data/'sessions'/secrets.token_hex(12);run.mkdir(parents=True,exist_ok=False)
    feed=run/'feed';session=secrets.randbits(64) or 1
    with config_lock(config):
        old={}
        if config.exists():
            raw,old=read_fields(config)
            status_path=Path(old.get('packet_path','')).parent/'status.local.json'
            try:
                status=json.loads(status_path.read_text())
                clock=ctypes.WinDLL('kernel32').GetTickCount64;clock.restype=ctypes.c_uint64
                if status.get('state') in ('starting','running') and 0<=clock()-status['tick_ms']<=2500:
                    raise RuntimeError('A minimap session is still active. Stop it in its session-control window first.')
            except (OSError,ValueError,KeyError,TypeError):pass
            (run/'previous-session.local.ini').write_bytes(raw)
        fields=session_fields(registry,old,session,feed)
        temporary=run/'prepared-session.local.ini'
        temporary.write_text(''.join(f'{k}={v}\n' for k,v in fields.items()),encoding='utf8')
        layout=run/'layout.local.json';atomic_json(layout,supported_layout())
        manifest={'schema':'srmm-launch-v1','pid':pid,'session':session,'map':fields['map'],'layout':str(layout),
                  'output_dir':str(feed),'config':str(temporary),'registry':str(Path(registry_path).resolve()),'record':record,'seconds':28800}
        if save:
            manifest.update(save=str(Path(save).resolve()),saved_waypoints_preview=True)
            if zones:manifest['zone_catalog']=str(Path(zones).resolve())
        path=run/'launch.local.json';atomic_json(path,manifest)
        # Use the controller's own validator against the prepared configuration.
        # The live configuration may not exist on a first install; publishing it
        # merely to validate the manifest would break failure atomicity.
        from feed_launcher import load_manifest
        load_manifest(path)
        manifest['config']=str(config);atomic_json(path,manifest)
        # Publish last, under the same lock: failed preparation cannot redirect
        # a working configuration to a feed that was never launched.
        os.replace(temporary,config)
    return path

def worker_command(command, log_path, frozen=None):
    if frozen is None:frozen=getattr(sys,'frozen',False)
    if not frozen:return command
    module=Path(command[1]).stem
    if module not in ('candidate_feed','map_switcher','destination_source','feed_launcher','startup_session'):
        raise ValueError('Unknown bundled worker.')
    return [command[0],'--worker',module,'--log',str(log_path),*command[2:]]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--game-root',type=Path,required=True);p.add_argument('--package',type=Path,required=True)
    p.add_argument('--registry',type=Path,required=True);p.add_argument('--save',type=Path);p.add_argument('--zones',type=Path)
    p.add_argument('--record',action='store_true');p.add_argument('--launch-game',action='store_true')
    a=p.parse_args()
    data=Path(os.environ['LOCALAPPDATA'])/'SnowRunnerMinimap';data.mkdir(parents=True,exist_ok=True)
    # Separate starter folder so this long-lived ownership lock never blocks config writers.
    lockdir=data/'startup';lockdir.mkdir(exist_ok=True)
    with starter_lock(lockdir):
        exe=verify_install(a.game_root,a.package);pid=running_install(exe)
        if pid is None and a.launch_game:
            k=ctypes.WinDLL('kernel32');k.SetDllDirectoryW.argtypes=[ctypes.c_wchar_p]
            k.SetDllDirectoryW(None)
            try:os.startfile('steam://rungameid/1465360')
            finally:
                if getattr(sys,'frozen',False):k.SetDllDirectoryW(sys._MEIPASS)
            deadline=time.monotonic()+120
            while pid is None and time.monotonic()<deadline:
                time.sleep(1);pid=running_install(exe)
        if pid is None:raise ValueError('SnowRunner is not running. Start this installation in Steam, then try again.')
        path=prepare_session(data,a.registry,pid,a.save,a.zones,a.record)
        log=path.parent/'controller.local.log'
        command=worker_command([sys.executable,str(Path(__file__).with_name('feed_launcher.py')),str(path),'--start'],log)
        with log.open('a',encoding='utf8') as output:
            result=subprocess.run(command,stdin=subprocess.DEVNULL,stdout=output,stderr=subprocess.STDOUT,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        if result.returncode:raise RuntimeError('Session controller failed. See '+str(log))

if __name__=='__main__':main()
