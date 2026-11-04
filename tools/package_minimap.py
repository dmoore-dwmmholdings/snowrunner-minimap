"""Create a new checksummed package from original locally built minimap DLLs.

No game assets, diagnostics or user data are copied. Package hashes check
integrity; distribute through a trusted release channel, not as authenticity proof.
"""
import argparse
import json
from pathlib import Path
from install_manager import DLLS, GAME_SHA256, MAX_DLL, digest, plain_path, checked_copy, package_files, require_x64_dll


def create_package(build_dir, output_dir, version):
    import re
    source = plain_path(build_dir); output = plain_path(output_dir)
    if not re.fullmatch(r'[A-Za-z0-9._-]{1,64}', version):
        raise ValueError('Version must be 1..64 ASCII letters, numbers, dots, underscores or hyphens.')
    hashes = {}
    for name in DLLS:
        path = plain_path(source / name)
        if not path.is_file() or not 64 <= path.stat().st_size <= MAX_DLL:
            raise ValueError('Missing or oversized locally built DLL: ' + name)
        require_x64_dll(path)
        hashes[name] = digest(path)
    output.mkdir(parents=True, exist_ok=False)
    for name in DLLS:
        checked_copy(source / name, output / name, hashes[name])
    manifest = {'schema':'srmm-install-package-v1','version':version,'game_sha256':GAME_SHA256,'files':hashes}
    (output / 'package.json').write_text(json.dumps(manifest,indent=2),encoding='utf8')
    package_files(output)
    return manifest


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,required=True)
    parser.add_argument('--output-dir',type=Path,required=True)
    parser.add_argument('--version',required=True)
    args=parser.parse_args()
    try:print(json.dumps(create_package(args.build_dir,args.output_dir,args.version),indent=2))
    except (OSError,ValueError) as error:parser.exit(1,str(error)+'\n')


if __name__=='__main__':main()
