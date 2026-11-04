"""Bounded, read-only saved-waypoint source for the diagnostic minimap.

This is NOT a live waypoint/objective reader. It exposes the last completed save
and its timestamp. The observed x/z * 150 conversion requires a marker alignment
check on this game build; the CLI therefore requires --allow-saved-preview.
No source path, save content, game assets or personal data is sent over a network.
"""
from dataclasses import dataclass
import argparse
import ctypes
import json
import math
from pathlib import Path
import re
import struct
import tempfile
import time

from probe_live_pose import EXPECTED_SHA256
from candidate_feed import replace_packet

MAGIC = b'SRMMDST1'
PACKET_BYTES = 672
MAX_POINTS = 16
MAX_SAVE_BYTES = 4 * 1024 * 1024
SAVED_WAYPOINT = 1
SAVED_OBJECTIVE = 2
SAVE_XZ_SCALE = 150.0
LEVEL = re.compile(r'level_[a-z0-9_]{3,48}')


@dataclass(frozen=True)
class SavedWaypoints:
    points: tuple  # world X/Z pairs, in saved route order
    saved_unix_ms: int
    kinds: tuple = ()  # Empty retains the saved-waypoint-only encoding.


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate save field.')
        result[key] = value
    return result


def read_bounded(path, maximum):
    with Path(path).open('rb') as source:
        data = source.read(maximum + 1)
    if len(data) > maximum:
        raise ValueError('Diagnostic input exceeds size bound.')
    return data


def parse_saved_cargo_targets(data, root_name):
    """Return zone references only for an understood one-stage saved objective.

    Multi-stage and non-cargo objectives have unresolved activity semantics and
    intentionally produce no targets. These are saved tracking state, not live.
    """
    if not isinstance(data, bytes) or not 0 < len(data) <= MAX_SAVE_BYTES:
        raise ValueError('Save size outside supported bound.')
    if not re.fullmatch(r'CompleteSave\d*', root_name):
        raise ValueError('Explicit save slot required.')
    payload = json.loads(data.rstrip(b'\0').decode('utf8'), object_pairs_hook=_unique_object)
    value = payload[root_name]['SslValue']
    tracked = value.get('trackedObjective')
    states = value.get('objectiveStates')
    if not isinstance(tracked, str) or not tracked or not isinstance(states, dict) or len(states) > 4096:
        return ()
    state = states.get(tracked)
    if not isinstance(state, dict) or state.get('id') != tracked or state.get('isFinished') is not False:
        return ()
    stages = state.get('stagesState')
    if not isinstance(stages, list) or len(stages) != 1 or not isinstance(stages[0], dict):
        return ()
    stage = stages[0]
    for key, item in stage.items():
        if key != 'cargoDeliveryActions' and not (item is None or isinstance(item, (list, dict)) and not item):
            return ()
    actions = stage.get('cargoDeliveryActions')
    if not isinstance(actions, list) or not 1 <= len(actions) <= 128:
        return ()
    targets = []
    seen = set()
    for action in actions:
        if not isinstance(action, dict):
            return ()
        cargo = action.get('cargoState')
        if not isinstance(cargo, dict):
            return ()
        current, goal = cargo.get('curValue'), cargo.get('aimValue')
        if any(type(v) not in (int, float) or not math.isfinite(v) for v in (current, goal)) or current < 0 or goal <= 0:
            return ()
        if current >= goal:
            continue
        level, zones = action.get('map'), action.get('zones')
        if (not isinstance(level, str) or not LEVEL.fullmatch(level) or not isinstance(zones, list) or not 1 <= len(zones) <= 32 or
            action.get('platformId') or action.get('truckUid') or action.get('isVisibleWithPlatform')):
            return ()
        for zone in zones:
            if not isinstance(zone, str) or not re.fullmatch(r'[A-Za-z0-9_.:-]{1,128}', zone):
                return ()
            identity = (level, zone)
            if identity not in seen:
                seen.add(identity);targets.append(identity)
    return tuple(targets)


def load_zone_catalog(path):
    data = json.loads(read_bounded(path, 8*1024*1024).decode('utf8'), object_pairs_hook=_unique_object)
    maps = data.get('maps')
    if data.get('schema') != 'srmm-zone-catalog-v1' or not isinstance(maps, dict) or not 1 <= len(maps) <= 128:
        raise ValueError('Invalid bounded static-zone catalog.')
    catalog = {}
    total = 0
    for level, entries in maps.items():
        if not LEVEL.fullmatch(level) or not isinstance(entries, list) or len(entries) > 1024:
            raise ValueError('Invalid zone map/count.')
        total += len(entries)
        if total > 16384:
            raise ValueError('Zone inventory exceeds bound.')
        locations = {}
        for entry in entries:
            if not isinstance(entry, dict):
                raise ValueError('Invalid zone record.')
            identifier, x, z = entry['id'], entry['x'], entry['z']
            if not isinstance(identifier, str) or not re.fullmatch(r'[A-Za-z0-9_.:-]{1,128}', identifier) or identifier in locations or entry.get('kind') != 'static_zone':
                raise ValueError('Invalid or ambiguous zone identifier.')
            if any(type(v) not in (int, float) or not math.isfinite(v) or abs(v) > 20000 for v in (x, z)):
                raise ValueError('Invalid zone position.')
            locations[identifier] = (x, z)
        catalog[level] = locations
    return catalog


def add_saved_objective_points(saved, data, root_name, level, catalog):
    points = list(saved.points)
    kinds = [SAVED_WAYPOINT] * len(points)
    for target_level, zone in parse_saved_cargo_targets(data, root_name):
        # A foreign map has a different coordinate frame. Never project it here.
        if target_level != level:
            continue
        point = catalog.get(level, {}).get(zone)
        if point is not None and len(points) < MAX_POINTS:
            points.append(point);kinds.append(SAVED_OBJECTIVE)
    return SavedWaypoints(tuple(points), saved.saved_unix_ms, tuple(kinds))


def parse_saved_waypoints(data, root_name, level, saved_unix_ms):
    """Extract only one explicit save slot and level; never choose newest slot."""
    if not isinstance(data, bytes) or not 0 < len(data) <= MAX_SAVE_BYTES:
        raise ValueError('Save size outside supported bound.')
    if not re.fullmatch(r'CompleteSave\d*', root_name) or not LEVEL.fullmatch(level):
        raise ValueError('Explicit save slot and canonical level required.')
    if type(saved_unix_ms) is not int or not 0 < saved_unix_ms < 2**64:
        raise ValueError('Invalid save timestamp.')
    # This release writes optional trailing NUL bytes after otherwise plain JSON.
    payload = json.loads(data.rstrip(b'\0').decode('utf8'), object_pairs_hook=_unique_object)
    slot = payload[root_name]['SslValue']
    waypoints = slot['waypoints']
    if not isinstance(waypoints, dict) or len(waypoints) > 256:
        raise ValueError('Invalid waypoint map table.')
    entries = waypoints.get(level, [])
    if not isinstance(entries, list) or len(entries) > 1024:
        raise ValueError('Waypoint count outside supported bound.')
    points = []
    for entry in entries:
        if not isinstance(entry, dict) or type(entry.get('type')) is not int or entry['type'] != 0:
            raise ValueError('Unsupported saved marker kind.')
        point = entry['point']
        x, z = point['x'], point['z']
        if any(type(value) not in (int, float) or not math.isfinite(value) for value in (x, z)):
            raise ValueError('Invalid waypoint position.')
        world = (x * SAVE_XZ_SCALE, z * SAVE_XZ_SCALE)
        if max(abs(value) for value in world) > 20000:
            raise ValueError('Waypoint position exceeds supported world bounds.')
        if len(points) < MAX_POINTS:
            points.append(world)
    return SavedWaypoints(tuple(points), saved_unix_ms)


class SavedWaypointSource:
    """Cache a coherent read, polling metadata without reopening unchanged saves."""
    def __init__(self, save_path, zone_catalog=None):
        self.path = Path(save_path)
        if self.path.suffix.lower() != '.cfg' or not re.fullmatch(r'CompleteSave\d*', self.path.stem):
            raise ValueError('Specify one explicit CompleteSave[slot].cfg file.')
        self._signature = None
        self._bytes = None
        self.zone_catalog = zone_catalog or {}

    def read(self, level):
        before = self.path.stat()
        if not 0 < before.st_size <= MAX_SAVE_BYTES:
            raise ValueError('Save size outside supported bound.')
        signature = (before.st_size, before.st_mtime_ns, before.st_ino)
        if signature != self._signature:
            with self.path.open('rb') as source:
                data = source.read(MAX_SAVE_BYTES + 1)
            after = self.path.stat()
            if (after.st_size, after.st_mtime_ns, after.st_ino) != signature or len(data) != before.st_size:
                raise ValueError('Save changed during read; retry next poll.')
            self._bytes, self._signature = data, signature
        saved = parse_saved_waypoints(self._bytes, self.path.stem, level, before.st_mtime_ns // 1000000)
        if self.zone_catalog:
            saved = add_saved_objective_points(saved, self._bytes, self.path.stem, level, self.zone_catalog)
        return saved


def encode_destinations(pid, session, sequence, tick_ms, build, level, saved):
    if (type(pid) is not int or not 0 < pid < 2**32 or
        any(type(value) is not int or not 0 < value < 2**64 for value in (session, sequence)) or
        type(tick_ms) is not int or not 0 <= tick_ms < 2**64 or
        not re.fullmatch('[a-f0-9]{64}', build) or not LEVEL.fullmatch(level)):
        raise ValueError('Invalid destination packet context.')
    points = () if saved is None else saved.points
    kinds = () if saved is None else saved.kinds or (SAVED_WAYPOINT,) * len(points)
    saved_ms = 0 if saved is None else saved.saved_unix_ms
    if len(points) > MAX_POINTS or len(kinds) != len(points) or any(type(kind) is not int or kind not in (SAVED_WAYPOINT,SAVED_OBJECTIVE) for kind in kinds) or (saved is not None and (type(saved_ms) is not int or not 0 < saved_ms < 2**64)):
        raise ValueError('Invalid saved destination snapshot.')
    packet = bytearray(PACKET_BYTES)
    struct.pack_into('<8s4I3Q64s32sQ2I', packet, 0, MAGIC, 1, PACKET_BYTES,
                     int(saved is not None), pid, sequence, tick_ms, session,
                     level.encode('ascii'), bytes.fromhex(build), saved_ms, SAVED_WAYPOINT, len(points))
    for index, point in enumerate(points):
        if len(point) != 2 or any(type(value) not in (float, int) or not math.isfinite(value) or abs(value) > 20000 for value in point):
            raise ValueError('Invalid destination coordinates.')
        struct.pack_into('<II2dQ', packet, 160 + index * 32, kinds[index], index + 1, *point, 0)
    return bytes(packet)


def pose_context(data, pid, session, now):
    """A saved point is only published for the same fresh active pose context."""
    if len(data) != 256 or data[:8] != b'SRMMTEL1':
        return None
    version, size, available, source_pid, sequence, tick, identity = struct.unpack_from('<4I3Q', data, 8)
    if (version != 1 or size != 256 or available != 1 or source_pid != pid or identity != session or
        not sequence or tick > now or now - tick > 750 or data[112:144] != bytes.fromhex(EXPECTED_SHA256) or any(data[192:])):
        return None
    name, zero, padding = data[48:112].partition(b'\0')
    if not zero or any(padding):
        return None
    try:
        level = name.decode('ascii')
    except UnicodeDecodeError:
        return None
    values = struct.unpack_from('<6d', data, 144)
    if (not LEVEL.fullmatch(level) or not all(math.isfinite(value) for value in values) or
        max(abs(value) for value in values[:3]) > 20000 or not .98 <= sum(value*value for value in values[3:]) <= 1.02):
        return None
    return level


def feed_run(data, pid, session, now, expected_run=None):
    """Bind helper lifetime to one fresh publisher run, including resumes."""
    if not isinstance(data, bytes) or not 0 < len(data) <= 16384:
        return None
    try:
        status = json.loads(data.decode('utf8'), object_pairs_hook=_unique_object)
        run = status['run'];tick = status['tick_ms']
        if (status.get('schema') != 'srmm-feed-status-v1' or status.get('pid') != pid or status.get('session') != session or
            status.get('state') != 'running' or not isinstance(run, str) or not re.fullmatch('[a-f0-9]{32}', run) or
            type(tick) is not int or tick > now or now - tick > 2000 or (expected_run is not None and run != expected_run)):
            return None
        return run
    except (ValueError, KeyError, TypeError, RecursionError):
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--save', type=Path, required=True)
    parser.add_argument('--zone-catalog', type=Path)
    parser.add_argument('--feed', type=Path, required=True)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--session', type=int, required=True)
    parser.add_argument('--seconds', type=int, default=600)
    parser.add_argument('--stop-file', type=Path)
    parser.add_argument('--feed-status', type=Path)
    parser.add_argument('--allow-saved-preview', action='store_true')
    args = parser.parse_args()
    if not args.allow_saved_preview:
        parser.error('Saved-waypoint scaling is pending local alignment validation; --allow-saved-preview is required.')
    if not 1 <= args.seconds <= 28800 or not 0 < args.pid < 2**32 or not 0 < args.session < 2**64:
        parser.error('Invalid session/PID or duration (1..28800 seconds).')
    feed = args.feed.resolve(strict=True)
    if not feed.is_dir() or feed.is_relative_to(args.save.resolve().parent):
        parser.error('Output must be a pre-existing diagnostic feed folder outside the save directory.')
    identity = json.loads(read_bounded(feed / 'session.local.json', 16384).decode('utf8'))
    if any(identity.get(key) != value for key, value in {'pid':args.pid, 'session':args.session, 'build':EXPECTED_SHA256}.items()):
        parser.error('Feed manifest belongs to another context.')
    last_level = identity['map']
    if not LEVEL.fullmatch(last_level):
        parser.error('Invalid feed map identity.')
    source = SavedWaypointSource(args.save, load_zone_catalog(args.zone_catalog) if args.zone_catalog else None)
    destination = feed / 'destinations.local.bin'
    sequence = 0
    import msvcrt
    with (feed / 'destinations.local.lock').open('a+b') as lock:
        lock.seek(0);msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        if destination.exists():
            prior = read_bounded(destination, PACKET_BYTES)
            if (len(prior) != PACKET_BYTES or prior[:8] != MAGIC or
                struct.unpack_from('<I', prior, 20)[0] != args.pid or struct.unpack_from('<Q', prior, 40)[0] != args.session or
                prior[112:144] != bytes.fromhex(EXPECTED_SHA256)):
                parser.error('Existing destination packet belongs to another context.')
            sequence = struct.unpack_from('<Q', prior, 24)[0]
        clock = ctypes.WinDLL('kernel32').GetTickCount64
        clock.restype = ctypes.c_uint64
        publisher_run = None
        if args.feed_status:
            publisher_run = feed_run(read_bounded(args.feed_status, 16384), args.pid, args.session, clock())
            if publisher_run is None:
                parser.error('A fresh matching running feed status is required.')
        def publish(level, saved):
            nonlocal sequence
            sequence += 1
            payload = encode_destinations(args.pid, args.session, sequence, clock(), EXPECTED_SHA256, level, saved)
            temporary = None
            try:
                with tempfile.NamedTemporaryFile(dir=feed, prefix='destinations-next-', suffix='.local.tmp', delete=False) as stream:
                    temporary = Path(stream.name)
                    stream.write(payload)
                replace_packet(temporary, destination)
            finally:
                if temporary is not None and temporary.exists():
                    temporary.unlink()
        deadline = time.monotonic() + args.seconds
        try:
            while time.monotonic() < deadline:
                if args.stop_file and args.stop_file.exists():
                    break
                if args.feed_status:
                    try:
                        current_run = feed_run(read_bounded(args.feed_status, 16384), args.pid, args.session, clock(), publisher_run)
                    except (OSError, ValueError):
                        current_run = None
                    if current_run is None:
                        break
                saved = None
                try:
                    packet = read_bounded(feed / 'candidate.local.bin', 256)
                    level = pose_context(packet, args.pid, args.session, clock())
                    if level:
                        last_level = level
                        saved = source.read(level)
                        # A save read may straddle a level transition. Recheck before publishing.
                        current = read_bounded(feed / 'candidate.local.bin', 256)
                        if pose_context(current, args.pid, args.session, clock()) != level:
                            saved = None
                except (OSError, ValueError, KeyError, TypeError, RecursionError):
                    saved = None
                publish(last_level, saved)
                time.sleep(.25)
        finally:
            publish(last_level, None)
    print('Saved-waypoint preview publisher stopped; final snapshot unavailable.')


if __name__ == '__main__':
    main()
