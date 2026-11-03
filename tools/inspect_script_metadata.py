"""Read a narrow, inferred SSLB metadata schema; never execute or emit bytecode.

Record roles and type shapes are hypotheses for the observed release variant,
not a published ABI. A bodyless declaration is only a native-binding candidate.
"""
import argparse
import json
import struct
import zipfile
from pathlib import Path

from inspect_script_bundle import MODULE_PREFIX, parse_bundle, read_archive_bundle

DEFAULT_MODULES = (
    'game.game_states.game_state_minimap.minimap_info_provider',
    'game.level_data.level_data',
    'game.minimap',
)
MAX_MODULE_BYTES = 1024 * 1024


def fields(blob):
    """Decode bounded protobuf-style wire fields without guessing nested blobs."""
    position = 0

    def take(size):
        nonlocal position
        if size > len(blob) - position:
            raise ValueError('Truncated metadata field.')
        value = blob[position:position + size]
        position += size
        return value

    def varint():
        value = 0
        for shift in range(0, 70, 7):
            byte = take(1)[0]
            if shift == 63 and byte > 1:
                raise ValueError('Varint exceeds uint64.')
            value |= (byte & 127) << shift
            if byte < 128:
                return value
        raise ValueError('Unterminated varint.')

    result = []
    while position < len(blob):
        tag = varint()
        number, wire = tag >> 3, tag & 7
        if not 1 <= number < (1 << 29):
            raise ValueError('Invalid field number.')
        if wire == 0:
            value = varint()
        elif wire == 2:
            value = take(varint())
        elif wire in (1, 5):
            value = take(8 if wire == 1 else 4)
        else:
            raise ValueError('Unsupported wire type.')
        result.append((number, wire, value))
        if len(result) > 65536:
            raise ValueError('Too many metadata fields.')
    return result


def values(record, number, wire):
    hits = [(w, v) for n, w, v in record if n == number]
    if any(w != wire for w, _ in hits):
        raise ValueError('Unexpected metadata wire type.')
    return [v for _, v in hits]


def one(record, number, wire, default=None):
    hits = values(record, number, wire)
    if len(hits) > 1:
        raise ValueError('Duplicate singular metadata field.')
    return hits[0] if hits else default


def at(items, index):
    if not isinstance(index, int) or not 0 <= index < len(items):
        raise ValueError('Metadata index out of bounds.')
    return items[index]


def inspect_module(chunk):
    if not 16 <= len(chunk) <= MAX_MODULE_BYTES or not chunk.startswith(MODULE_PREFIX):
        raise ValueError('Unsupported SSLB module.')
    if struct.unpack_from('<I', chunk, 12)[0] != len(chunk) - 16:
        raise ValueError('SSLB payload size mismatch.')
    root = fields(chunk[16:])
    strings = [v.decode('utf-8') for v in values(root, 6, 2)]
    catalog = fields(one(root, 4, 2, b''))
    imports = [at(strings, one(fields(v), 1, 0)) for v in values(catalog, 1, 2)]
    types = [fields(v) for v in values(root, 5, 2)]
    definitions = [fields(v) for v in values(root, 7, 2)]
    functions = [fields(v) for v in values(root, 8, 2)]

    def shape(index, seen=()):
        if index in seen or len(seen) >= 16:
            return {'unresolvedType': index}
        record = at(types, index)
        seen = seen + (index,)
        kind = one(record, 1, 0)
        if kind == 1:
            return at(strings, one(at(definitions, one(record, 2, 0)), 1, 0))
        if kind == 3:
            return at(imports, one(record, 2, 0))
        if kind == 6:
            return at(strings, one(record, 3, 0))
        if kind == 4:
            ref = fields(one(record, 9, 2, b''))
            name = at(imports, one(ref, 1, 0))
            args = [shape(i, seen) for i in values(ref, 2, 0)]
            return {'generic': name, 'arguments': args} if args else name
        if kind in (9, 10):
            ref = fields(one(record, 4 if kind == 9 else 6, 2, b''))
            target = shape(one(ref, 1, 0), seen)
            args = [shape(i, seen) for i in values(ref, 2 if kind == 9 else 3, 0)]
            return ({'return': target, 'parameters': args} if kind == 9
                    else {'instantiation': target, 'arguments': args})
        return {'unresolvedType': index, 'kind': kind}

    declarations = []
    for owner in definitions:
        for raw in values(owner, 3, 2):
            member = fields(raw)
            kind = one(member, 3, 0)
            if kind not in (1, 2):
                continue
            function = at(functions, one(member, 4, 0))
            signature = fields(one(function, 2, 2, b''))
            name = at(strings, one(member, 1, 0))
            if name != at(strings, one(signature, 1, 0)):
                raise ValueError('Declaration/function name disagreement.')
            # Field 5 is opaque: presence only, no bytecode decoding or output.
            body = one(function, 5, 2)
            declarations.append({
                'owner': at(strings, one(owner, 1, 0)), 'name': name,
                'slot': one(member, 2, 0), 'memberKind': kind,
                'hasBodyField': body is not None,
                'shapeCandidate': shape(one(function, 1, 0)),
                'parameterNames': [at(strings, one(fields(p), 1, 0))
                                   for p in values(signature, 2, 2)],
            })
    references = []
    for raw in values(catalog, 4, 2):
        ref = fields(raw)
        references.append({
            'name': at(strings, one(ref, 2, 0)),
            'ownerTypeCandidate': shape(one(ref, 3, 0)),
            'slot': one(ref, 4, 0), 'shapeCandidate': shape(one(ref, 5, 0)),
        })
    return {'declarations': declarations, 'references': references}


def inspect_archive(path, member, selected):
    blob = read_archive_bundle(path, member)
    modules = {m['name']: m for m in parse_bundle(blob)}
    results = []
    for name in selected:
        if name not in modules:
            raise ValueError('Requested module absent: ' + name)
        module = modules[name]
        start = module['offset']
        results.append({**module, **inspect_module(blob[start:start + module['bytes']])})
    return {'schema': 'snowrunner-script-metadata-candidates-v1', 'modules': results,
            'limitations': ['Inferred schema; declarations and references are record-role candidates.',
                            'Bodyless records do not prove native implementation or external accessibility.',
                            'Type shapes do not establish ABI, units, lifetimes or a mod loader.',
                            'No bytecode, runtime addresses or game assets are emitted.']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--member', default='[ssl_cache]/initial_release.sslbundle')
    parser.add_argument('--module', action='append', dest='modules')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        report = inspect_archive(args.archive, args.member, args.modules or DEFAULT_MODULES)
        text = json.dumps(report, indent=2) + '\n'
        if args.output:
            with args.output.open('x', encoding='utf-8') as stream:
                stream.write(text)
        else:
            print(text, end='')
    except (OSError, ValueError, RuntimeError, zipfile.BadZipFile) as error:
        parser.exit(1, f'Inspection failed: {error}\n')


if __name__ == '__main__':
    main()
