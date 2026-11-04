"""Portable setup entry point and allowlisted bundled worker dispatcher."""
import importlib, os, sys
from pathlib import Path

WORKERS=('candidate_feed','map_switcher','destination_source','feed_launcher','startup_session')

def main():
    if len(sys.argv)>1 and sys.argv[1]=='--worker':
        if len(sys.argv)<5 or sys.argv[2] not in WORKERS or sys.argv[3]!='--log':
            raise ValueError('Invalid worker invocation.')
        module=sys.argv[2];log=Path(sys.argv[4])
        with log.open('a',encoding='utf8',buffering=1) as output:
            sys.stdout=sys.stderr=output
            sys.argv=[module,*sys.argv[5:]]
            try:importlib.import_module(module).main()
            except Exception:
                import traceback
                traceback.print_exc();raise
        return
    if len(sys.argv)>1:
        # Packaging smoke test: does not access a game, start helpers or create UI.
        if sys.argv[1:] == ['--self-check']:
            for name in WORKERS:importlib.import_module(name)
            import setup_minimap,install_manager,import_map_registry,map_zones
            return
        raise ValueError('Unknown option.')
    from setup_minimap import main as setup
    if sys.stdout is None or sys.stderr is None:
        logs=Path(os.environ['LOCALAPPDATA'])/'SnowRunnerMinimap';logs.mkdir(parents=True,exist_ok=True)
        sys.stdout=sys.stderr=(logs/'setup.local.log').open('a',encoding='utf8',buffering=1)
    setup()

if __name__=='__main__':main()
