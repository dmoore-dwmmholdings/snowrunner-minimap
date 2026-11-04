"""Local lifetime controls for an explicitly selected diagnostic game process.

No service registration, process discovery, job escape, privilege changes or game
writes. The status/stop files carry identity and health, never vehicle coordinates.
"""
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import secrets
import time


MAX_SESSION_SECONDS = 8 * 60 * 60


def read_json(path, maximum=16384):
    with Path(path).open('rb') as source:
        raw = source.read(maximum + 1)
    if len(raw) > maximum:
        raise ValueError('Local control file exceeds size bound.')
    value = json.loads(raw)
    if not isinstance(value, dict):
        raise ValueError('Expected a local control object.')
    return value


def atomic_json(path, value):
    """Create our own temporary file; ordinary rename conflicts remain errors."""
    path = Path(path)
    payload = json.dumps(value, separators=(',', ':')).encode('utf8')
    if len(payload) > 16384:
        raise ValueError('Local status exceeds size bound.')
    temporary = path.with_name(f'{path.name}.{secrets.token_hex(8)}.tmp')
    try:
        with temporary.open('xb') as output:
            output.write(payload)
        for attempt in range(11):
            try:
                os.replace(temporary, path)
                break
            except OSError as error:
                if getattr(error, 'winerror', None) not in (5, 32) or attempt == 10:
                    raise
                time.sleep(.025)
    finally:
        if temporary.exists():
            temporary.unlink()


def stop_requested(path, pid, session, run):
    try:
        request = read_json(path)
        return request == {'schema': 'srmm-feed-stop-v1', 'pid': pid,
                           'session': session, 'run': run}
    except (OSError, ValueError, TypeError):
        return False


def request_stop(directory, status):
    if status.get('schema') != 'srmm-feed-status-v1' or status.get('state') not in ('starting', 'running'):
        raise ValueError('No active identified feed to stop.')
    pid, session, run = status.get('pid'), status.get('session'), status.get('run')
    if type(pid) is not int or not 0 < pid < 2**32 or type(session) is not int or not 0 < session < 2**64 or not isinstance(run, str) or len(run) != 32:
        raise ValueError('Invalid feed identity.')
    atomic_json(Path(directory)/'stop.local.json',
                {'schema': 'srmm-feed-stop-v1', 'pid': pid, 'session': session, 'run': run})


class ProcessLifetime:
    """Use the already opened read-only handle: a reused PID cannot change target."""
    def __init__(self, handle):
        self.handle = handle
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        self.kernel.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
        self.kernel.GetExitCodeProcess.restype = wintypes.BOOL

    def alive(self):
        code = wintypes.DWORD()
        if not self.kernel.GetExitCodeProcess(self.handle, ctypes.byref(code)):
            raise OSError(ctypes.get_last_error(), 'Cannot inspect selected game lifetime.')
        return code.value == 259  # STILL_ACTIVE


def run_samples(sample, publish, alive, stopped, seconds, clock=time.monotonic,
                pause=time.sleep, interval=.05, update=lambda *_: None):
    """Always revoke the last packet, including sample/status/write exceptions."""
    if not 0 < seconds <= MAX_SESSION_SECONDS or not .02 <= interval <= 1:
        raise ValueError('Invalid bounded publisher lifetime or cadence.')
    deadline = clock() + seconds
    reason = 'error'
    last_level = None
    try:
        while True:
            started = clock()
            if started >= deadline:
                reason = 'duration_limit'; break
            if stopped():
                reason = 'stop_requested'; break
            if not alive():
                reason = 'game_exited'; break
            last_level, pose, tick = sample()
            publish(last_level, pose, tick)
            update('running', last_level, pose is not None)
            pause(max(0, interval - (clock() - started)))
    except KeyboardInterrupt:
        reason = 'interrupted'
    finally:
        try:
            publish(last_level, None, None)
        finally:
            update(reason, last_level, False)
    return reason
