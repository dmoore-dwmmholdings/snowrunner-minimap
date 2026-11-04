"""Build an offline Windows folder bundle using an already installed PyInstaller.

No downloads, cloud CI, game assets, private profiles or machine-specific paths.
"""
import argparse,hashlib,json,os,shutil,subprocess,sys
from pathlib import Path
from package_minimap import create_package

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--native-build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--version',required=True)
    a=p.parse_args();out=a.output.resolve()
    if out.exists():raise ValueError('Use a new output directory.')
    out.mkdir(parents=True);tools=Path(__file__).resolve().parent
    hidden=['candidate_feed','map_switcher','destination_source','feed_launcher','startup_session','setup_minimap','install_manager','import_map_registry','map_zones']
    cmd=[sys.executable,'-m','PyInstaller','--noconfirm','--clean','--onedir','--windowed','--name','SnowRunnerMinimap',
         '--distpath',str(out/'dist'),'--workpath',str(out/'work'),'--specpath',str(out/'spec'),'--paths',str(tools)]
    for name in hidden:cmd.extend(['--hidden-import',name])
    cmd.append(str(tools/'snowrunner_minimap.py'))
    with (out/'build.log').open('w',encoding='utf8') as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
    app=out/'dist/SnowRunnerMinimap'
    create_package(a.native_build,app/'package',a.version)
    for name in ('portable-setup.md','installer.md','game-settings-integration.md'):
        source=tools.parent/'docs'/name
        if source.exists():shutil.copy2(source,app/name)
    notices=app/'runtime-licenses';notices.mkdir()
    for source in [Path(sys.base_prefix)/'LICENSE.txt',*sorted((Path(sys.base_prefix)/'tcl').glob('*/license.terms'))]:
        if source.is_file():shutil.copy2(source,notices/(source.parent.name+'-'+source.name))
    hashes={str(f.relative_to(app)).replace('\\','/'):hashlib.file_digest(f.open('rb'),'sha256').hexdigest() for f in app.rglob('*') if f.is_file()}
    (out/'sha256.json').write_text(json.dumps(hashes,indent=2))
    print(str(app))

if __name__=='__main__':main()
