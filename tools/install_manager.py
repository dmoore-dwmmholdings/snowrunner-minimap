"""Explicit, reversible local installer for original SnowRunner minimap DLLs.

Plans by default. No elevation, downloads, game launch, save/config changes or
proprietary assets. Hashes provide integrity, not publisher authentication.
"""
import argparse
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import struct
import sys
import uuid
from contextlib import contextmanager

GAME_SHA256 = 'd46623b23808d19a40f7ebeec15332d11f97317637ec4d4e072a501bc78a77c8'
DLLS = ('d3d11.dll', 'dxgi.dll')
MAX_DLL = 32 * 1024 * 1024
# Only this previously produced original development pair may be explicitly
# migrated. Package manifests and callers cannot add trusted legacy hashes.
LEGACY_BUILDS = {
    'cbf9772': {
        'd3d11.dll': 'a00f55fbbcffc5cdf5fe82d26668f3903aa71f1f50f803ac47ea34ad08a93344',
        'dxgi.dll': 'e724d7e0947a7e26ff51898ddd7bbb39300540798744c1204147ec63903f141d',
    },
}


def digest(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def require_x64_dll(path):
    with Path(path).open('rb') as source:
        header = source.read(64)
        if len(header) != 64 or header[:2] != b'MZ':
            raise ValueError('Invalid DLL DOS header.')
        offset = struct.unpack_from('<I', header, 60)[0]
        if not 64 <= offset <= 1024 * 1024:
            raise ValueError('Invalid bounded PE offset.')
        source.seek(offset); pe = source.read(26)
    if (len(pe) != 26 or pe[:4] != b'PE\0\0' or struct.unpack_from('<H', pe, 4)[0] != 0x8664 or
        struct.unpack_from('<H', pe, 20)[0] < 2 or not struct.unpack_from('<H', pe, 22)[0] & 0x2000 or
        struct.unpack_from('<H', pe, 24)[0] != 0x20b):
        raise ValueError('Package must contain x64 PE32+ DLLs.')


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate JSON key.')
        result[key] = value
    return result


def read_json(path):
    with Path(path).open('rb') as source:
        data = source.read(65537)
    if len(data) > 65536:
        raise ValueError('Manifest exceeds 64 KiB.')
    value = json.loads(data, object_pairs_hook=unique_object)
    if not isinstance(value, dict):
        raise ValueError('Manifest must be an object.')
    return value


def plain_path(path):
    """Reject symlinks/junctions in every existing component before resolving."""
    path = Path(os.path.abspath(path))
    for component in (path, *path.parents):
        if component.is_symlink() or (hasattr(component, 'is_junction') and component.is_junction()):
            raise ValueError('Symbolic links and junctions are not supported: ' + str(component))
    return path.resolve()


def atomic_json(path, value):
    temporary = path.with_name(path.name + '.' + uuid.uuid4().hex + '.tmp')
    try:
        with temporary.open('x', encoding='utf8') as target:
            json.dump(value, target, indent=2)
            target.flush()
            os.fsync(target.fileno())
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def game_running():
    if os.name != 'nt':
        raise ValueError('Apply requires Windows. Planning is portable.')
    result = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq SnowRunner.exe', '/FO', 'CSV', '/NH'],
                            capture_output=True, text=True, check=True, timeout=15,
                            creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    return any(row and row[0].casefold() == 'snowrunner.exe' for row in csv.reader(io.StringIO(result.stdout)))


def ensure_closed(running):
    if running():
        raise ValueError('SnowRunner is running. Exit normally before installing, recovering or uninstalling.')


def package_files(package):
    package = plain_path(package)
    manifest = read_json(plain_path(package / 'package.json'))
    if (manifest.get('schema') != 'srmm-install-package-v1' or
        manifest.get('game_sha256') != GAME_SHA256 or
        not isinstance(manifest.get('version'), str) or
        not re.fullmatch(r'[A-Za-z0-9._-]{1,64}', manifest['version']) or
        not isinstance(manifest.get('files'), dict) or set(manifest['files']) != set(DLLS)):
        raise ValueError('Unsupported package schema, file list or game build.')
    for name, expected in manifest['files'].items():
        path = plain_path(package / name)
        if (not isinstance(expected, str) or not re.fullmatch('[a-f0-9]{64}', expected) or
            not path.is_file() or not 64 <= path.stat().st_size <= MAX_DLL or digest(path) != expected):
            raise ValueError('Package file missing, outside bounds or checksum mismatch: ' + name)
        require_x64_dll(path)
    return package, manifest


def target_paths(game_dir, state_dir, require_supported=True):
    game = plain_path(game_dir)
    target = plain_path(game / 'Sources' / 'Bin')
    exe = plain_path(target / 'SnowRunner.exe')
    if not exe.is_file() or (require_supported and digest(exe) != GAME_SHA256):
        raise ValueError('Unsupported or missing SnowRunner.exe. This release supports one fingerprint; do not override it.')
    state = plain_path(state_dir)
    if state.is_relative_to(game) or game.is_relative_to(state):
        raise ValueError('Installer state must be a separate private directory outside the game tree.')
    for name in DLLS:
        plain_path(target / name)
    return game, target, state


def load_receipt(state, game):
    path = plain_path(state / 'installed.json')
    if not path.exists():
        return None
    receipt = read_json(path)
    if (receipt.get('schema') != 'srmm-owned-install-v1' or receipt.get('game') != str(game) or
        not isinstance(receipt.get('files'), dict) or set(receipt['files']) != set(DLLS) or
        any(not isinstance(v, str) or not re.fullmatch('[a-f0-9]{64}', v) for v in receipt['files'].values())):
        raise ValueError('Installer ownership receipt is invalid or belongs to another game directory.')
    return receipt


def current_hash(path):
    plain_path(path)
    if not path.exists():
        return None
    if not path.is_file():
        raise ValueError('Expected an ordinary DLL file: ' + str(path))
    return digest(path)


def plan_install(game_dir, package_dir, state_dir):
    game, target, state = target_paths(game_dir, state_dir)
    package, manifest = package_files(package_dir)
    if (state / 'transaction.json').exists():
        raise ValueError('An unfinished transaction exists. Run recover before continuing.')
    receipt = load_receipt(state, game)
    for name in DLLS:
        expected = receipt['files'][name] if receipt else None
        if current_hash(target / name) != expected:
            raise ValueError('Unknown, modified or missing existing DLL; nothing will be overwritten: ' + name)
    return {'operation': 'upgrade' if receipt else 'install', 'game': str(game), 'target': str(target),
            'state': str(state), 'package': str(package), 'version': manifest['version'],
            'files': manifest['files'], 'previous_receipt': receipt}


def plan_adopt_legacy(game_dir, state_dir):
    """Read-only proposal; ordinary install never silently adopts legacy DLLs."""
    game, target, state = target_paths(game_dir, state_dir)
    if (state / 'transaction.json').exists():
        raise ValueError('An unfinished transaction exists. Run recover before legacy migration.')
    if (state / 'installed.json').exists():
        raise ValueError('An ownership receipt already exists. Use normal upgrade, not legacy migration.')
    hashes = {name: current_hash(target / name) for name in DLLS}
    matches = [version for version, known in LEGACY_BUILDS.items() if hashes == known]
    if len(matches) != 1:
        raise ValueError('Legacy migration requires the exact known original DLL pair; unknown, partial or modified pairs are refused.')
    for name in DLLS:
        require_x64_dll(target / name)
    return {'operation': 'adopt_legacy', 'game': str(game), 'target': str(target),
            'state': str(state), 'version': matches[0], 'files': hashes,
            'note': 'Explicit migration records ownership and verified backups only; DLLs remain unchanged.'}


def adopt_legacy(game_dir, state_dir, running=game_running, fault=lambda _: None):
    """Explicitly adopt the allowlisted original pair, without modifying it.

    The UI must present plan_adopt_legacy and obtain specific migration consent.
    An interrupted state-only adoption uses the ordinary recover operation.
    """
    ensure_closed(running)
    plan = plan_adopt_legacy(game_dir, state_dir)
    state = Path(plan['state'])
    with transaction_lock(state):
        ensure_closed(running)
        plan = plan_adopt_legacy(game_dir, state_dir)
        target, game = Path(plan['target']), Path(plan['game'])
        identifier = uuid.uuid4().hex
        backup = state / ('backup-' + identifier)
        backup.mkdir()
        for name in DLLS:
            checked_copy(target / name, backup / name, plan['files'][name])
        receipt = {'schema': 'srmm-owned-install-v1', 'game': str(game),
                   'version': plan['version'], 'files': plan['files'],
                   'adopted_legacy': True, 'legacy_backup': str(backup)}
        journal = {'schema': 'srmm-install-transaction-v1', 'id': identifier,
                   'game': str(game), 'before': plan['files'], 'after': plan['files'],
                   'previous_receipt': None, 'proposed_receipt': receipt,
                   'operation': 'adopt_legacy'}
        atomic_json(state / 'transaction.json', journal)
        try:
            fault('backed_up')
            ensure_closed(running)
            if (state / 'installed.json').exists():
                raise ValueError('Ownership changed during legacy migration.')
            for name in DLLS:
                if current_hash(target / name) != plan['files'][name] or current_hash(backup / name) != plan['files'][name]:
                    raise ValueError('Legacy DLL or verified backup changed during migration: ' + name)
            atomic_json(state / 'installed.json', receipt)
            fault('receipt_written')
            atomic_json(state / ('completed-' + identifier + '.json'), journal)
            (state / 'transaction.json').unlink()
        except Exception as error:
            try:
                _recover(game, target, state, running)
            except Exception as recovery_error:
                raise RuntimeError('Legacy migration interrupted; recovery is pending. DLLs were not modified. ' + str(recovery_error)) from error
            raise RuntimeError('Legacy migration failed; prior ownership state was restored and DLLs were not modified: ' + str(error)) from error
        return receipt


@contextmanager
def transaction_lock(state):
    state.mkdir(parents=True, exist_ok=True)
    path = plain_path(state / 'operation.lock')
    with path.open('a+b') as lock:
        if os.name == 'nt':
            import msvcrt
            lock.seek(0)
            if not lock.read(1):
                lock.write(b'0'); lock.flush()
            lock.seek(0); msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        try:
            yield
        finally:
            if os.name == 'nt':
                lock.seek(0); msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)


def checked_copy(source, destination, expected):
    plain_path(source); plain_path(destination)
    with source.open('rb') as incoming, destination.open('xb') as outgoing:
        shutil.copyfileobj(incoming, outgoing)
        outgoing.flush(); os.fsync(outgoing.fileno())
    if digest(destination) != expected:
        raise ValueError('Copy checksum mismatch.')


def _recover(game, target, state, running):
    journal_path = plain_path(state / 'transaction.json')
    if not journal_path.exists():
        return {'status': 'nothing_to_recover'}
    j = read_json(journal_path)
    if (j.get('schema') != 'srmm-install-transaction-v1' or j.get('game') != str(game) or
        not re.fullmatch('[a-f0-9]{32}', j.get('id', '')) or
        not isinstance(j.get('before'), dict) or not isinstance(j.get('after'), dict) or
        set(j['before']) != set(DLLS) or set(j['after']) != set(DLLS)):
        raise ValueError('Invalid transaction. Preserve files for manual review.')
    for values in (j['before'], j['after']):
        if any(v is not None and (not isinstance(v, str) or not re.fullmatch('[a-f0-9]{64}', v)) for v in values.values()):
            raise ValueError('Invalid transaction hashes.')
    backups = plain_path(state / ('backup-' + j['id']))
    previous = j.get('previous_receipt')
    proposed = j.get('proposed_receipt')
    for receipt in (previous, proposed):
        if receipt is not None and (not isinstance(receipt, dict) or receipt.get('schema') != 'srmm-owned-install-v1' or
            receipt.get('game') != str(game) or receipt.get('files') not in (j['before'], j['after'])):
            raise ValueError('Invalid transaction ownership receipt.')
    existing_receipt = read_json(state / 'installed.json') if (state / 'installed.json').exists() else None
    if existing_receipt not in (previous, proposed):
        raise ValueError('Ownership receipt changed; preserve all files for manual review.')
    stages = [target / ('.srmm-install-' + j['id'] + '-' + name + '.tmp') for name in DLLS]
    for name, staged in zip(DLLS, stages):
        if staged.exists() and current_hash(staged) != j['after'][name]:
            raise ValueError('Staged file changed or incomplete; preserve it for manual review: ' + str(staged))
    for name in DLLS:
        if current_hash(target / name) not in (j['before'][name], j['after'][name]):
            raise ValueError('Recovery found a foreign or modified DLL; leaving all files unchanged: ' + name)
        if j['before'][name] is not None and current_hash(backups / name) != j['before'][name]:
            raise ValueError('Recovery backup missing or changed: ' + name)
    ensure_closed(running)
    for name in DLLS:
        destination = target / name
        if current_hash(destination) == j['before'][name]:
            continue
        if current_hash(destination) != j['after'][name]:
            raise ValueError('DLL changed during recovery; stopped.')
        ensure_closed(running)
        if j['before'][name] is None:
            destination.unlink()
        else:
            temporary = target / ('.srmm-recover-' + uuid.uuid4().hex + '.tmp')
            checked_copy(backups / name, temporary, j['before'][name])
            os.replace(temporary, destination)
    if previous is None:
        if (state / 'installed.json').exists():
            (state / 'installed.json').unlink()
    else:
        atomic_json(state / 'installed.json', previous)
    atomic_json(state / ('recovered-' + j['id'] + '.json'), j)
    for staged in stages:
        if staged.exists():
            staged.unlink()
    journal_path.unlink()
    return {'status': 'recovered', 'backup': str(backups)}


def recover(game_dir, state_dir, running=game_running):
    game, target, state = target_paths(game_dir, state_dir, require_supported=False)
    ensure_closed(running)
    with transaction_lock(state):
        return _recover(game, target, state, running)


def _apply(game, target, state, before, after, previous, proposed, package, running, fault=lambda _: None):
    identifier = uuid.uuid4().hex
    backup = state / ('backup-' + identifier); backup.mkdir()
    for name in DLLS:
        if before[name] is not None:
            checked_copy(target / name, backup / name, before[name])
    journal = {'schema': 'srmm-install-transaction-v1', 'id': identifier, 'game': str(game),
               'before': before, 'after': after, 'previous_receipt': previous, 'proposed_receipt': proposed}
    atomic_json(state / 'transaction.json', journal)
    try:
        for name in DLLS:
            ensure_closed(running)
            if current_hash(target / name) != before[name]:
                raise ValueError('DLL changed after planning; stopped: ' + name)
            if after[name] is None:
                (target / name).unlink()
            else:
                staged = target / ('.srmm-install-' + identifier + '-' + name + '.tmp')
                checked_copy(package / name, staged, after[name])
                if current_hash(target / name) != before[name]:
                    raise ValueError('DLL changed during staging; stopped: ' + name)
                ensure_closed(running)
                os.replace(staged, target / name)
            fault(name)
        if proposed is None:
            (state / 'installed.json').unlink()
        else:
            atomic_json(state / 'installed.json', proposed)
        atomic_json(state / ('completed-' + identifier + '.json'), journal)
        (state / 'transaction.json').unlink()
    except Exception as error:
        try:
            _recover(game, target, state, running)
        except Exception as recovery_error:
            raise RuntimeError('Operation interrupted; recovery is pending. Keep game closed and run recover. ' + str(recovery_error)) from error
        raise RuntimeError('Operation failed and previous owned state was restored: ' + str(error)) from error
    return {'status': 'installed' if proposed else 'uninstalled', 'backup': str(backup), 'game': str(game)}


def install(game_dir, package_dir, state_dir, running=game_running, fault=lambda _: None):
    ensure_closed(running)
    plan = plan_install(game_dir, package_dir, state_dir)
    state = Path(plan['state'])
    with transaction_lock(state):
        plan = plan_install(game_dir, package_dir, state_dir)
        previous = plan['previous_receipt']
        before = previous['files'] if previous else dict.fromkeys(DLLS)
        proposed = {'schema': 'srmm-owned-install-v1', 'game': plan['game'], 'version': plan['version'], 'files': plan['files']}
        return _apply(Path(plan['game']), Path(plan['target']), state, before, plan['files'], previous,
                      proposed, Path(plan['package']), running, fault)


def plan_uninstall(game_dir, state_dir):
    game, target, state = target_paths(game_dir, state_dir, require_supported=False)
    if (state / 'transaction.json').exists():
        raise ValueError('Unfinished transaction: run recover first.')
    receipt = load_receipt(state, game)
    if receipt is None:
        raise ValueError('No ownership receipt. Refusing to infer ownership from filenames.')
    for name in DLLS:
        if current_hash(target / name) != receipt['files'][name]:
            raise ValueError('Owned DLL was changed or removed; nothing will be deleted: ' + name)
    return {'operation': 'uninstall', 'game': str(game), 'target': str(target), 'state': str(state), 'previous_receipt': receipt}


def uninstall(game_dir, state_dir, running=game_running, fault=lambda _: None):
    ensure_closed(running)
    plan = plan_uninstall(game_dir, state_dir); state = Path(plan['state'])
    with transaction_lock(state):
        plan = plan_uninstall(game_dir, state_dir); previous = plan['previous_receipt']
        return _apply(Path(plan['game']), Path(plan['target']), state, previous['files'], dict.fromkeys(DLLS),
                      previous, None, None, running, fault)


def steam_candidates(steam_dir):
    """Read only Steam library metadata; return choices, never choose a newest save."""
    steam = plain_path(steam_dir)
    libraries = [steam]
    vdf = steam / 'steamapps' / 'libraryfolders.vdf'
    if vdf.exists():
        if vdf.stat().st_size > 1024 * 1024:
            raise ValueError('Steam library index exceeds bound.')
        text = vdf.read_text(encoding='utf8')
        for raw in re.findall(r'"path"\s+"((?:\\.|[^"\\])*)"', text):
            decoded = raw.replace('\\\\', '\\').replace('\\"', '"')
            if not Path(decoded).is_absolute():
                raise ValueError('Steam library path must be absolute.')
            libraries.append(plain_path(decoded))
    results = []
    for library in dict.fromkeys(libraries):
        manifest = library / 'steamapps' / 'appmanifest_1465360.acf'
        if not manifest.is_file():
            continue
        if manifest.stat().st_size > 65536:
            raise ValueError('Steam app manifest exceeds bound.')
        matches = re.findall(r'"installdir"\s+"([^"\\/]+)"', manifest.read_text(encoding='utf8'))
        if len(matches) != 1 or matches[0] in ('.', '..') or re.search(r'[<>:"|?*\x00-\x1f]', matches[0]):
            raise ValueError('Ambiguous Steam install directory.')
        candidate = plain_path(library / 'steamapps' / 'common' / matches[0])
        if (candidate / 'Sources' / 'Bin' / 'SnowRunner.exe').is_file():
            results.append(str(candidate))
    return list(dict.fromkeys(results))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=('discover', 'install', 'uninstall', 'recover'))
    parser.add_argument('--game-dir', type=Path)
    parser.add_argument('--package', type=Path)
    parser.add_argument('--state-dir', type=Path)
    parser.add_argument('--steam-dir', type=Path)
    parser.add_argument('--apply', action='store_true', help='Perform the described local file changes; default is a read-only plan.')
    args = parser.parse_args()
    try:
        if args.operation == 'discover':
            if not args.steam_dir:raise ValueError('Specify --steam-dir (the Steam client installation).')
            result = {'candidates': steam_candidates(args.steam_dir)}
        else:
            if not args.game_dir or not args.state_dir:raise ValueError('Specify --game-dir and a dedicated --state-dir.')
            if args.operation == 'install':
                if not args.package:raise ValueError('Specify --package containing a trusted local build.')
                result = install(args.game_dir,args.package,args.state_dir) if args.apply else plan_install(args.game_dir,args.package,args.state_dir)
            elif args.operation == 'uninstall':
                result = uninstall(args.game_dir,args.state_dir) if args.apply else plan_uninstall(args.game_dir,args.state_dir)
            elif args.apply:
                result = recover(args.game_dir,args.state_dir)
            else:
                _, _, state = target_paths(args.game_dir,args.state_dir,require_supported=False)
                result = {'operation':'recover','pending':(state/'transaction.json').exists(),'note':'Use --apply only with the game closed; changed files are refused.'}
        print(json.dumps(result, indent=2))
    except (OSError,ValueError,RuntimeError,subprocess.SubprocessError) as error:
        parser.exit(1, str(error)+'\n')


if __name__ == '__main__':
    main()
