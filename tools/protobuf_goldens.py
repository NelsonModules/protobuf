# Golden files of the .m tests (tests/data/golden), produced with protoc and the Python protobuf runtime.
# Usage:
#   python -I protobuf_goldens.py descriptors <module dir>
#   python -I protobuf_goldens.py messages <module dir> <protoc.exe> [count]
import json
import os
import random
import struct
import subprocess
import sys
import tempfile

from google.protobuf import descriptor_pb2, descriptor_pool, message_factory
from google.protobuf.internal.encoder import _VarintBytes

FD = descriptor_pb2.FieldDescriptorProto


def dump_field(out, pad, f, fd, keyword='field'):
    # fd: pool field descriptor of a proto2 or editions file (resolved features), else None.
    packed = '-'
    if f.options.HasField('packed'):
        packed = 'true' if f.options.packed else 'false'
    oneof = str(f.oneof_index) if f.HasField('oneof_index') else '-'
    extendee = f'{f.extendee} ' if keyword == 'extension' else ''
    out.append(f'{pad}{keyword} {f.name} {f.number} {f.label} {f.type} '
               f'{f.type_name or "-"} {extendee}{f.json_name} {oneof} '
               f'{1 if f.proto3_optional else 0} {packed}')
    if fd is not None:
        utf8 = '-'
        if fd.type == fd.TYPE_STRING:
            utf8 = '1' if fd._GetFeatures().utf8_validation != 3 else '0'
        required = 1 if (fd._GetFeatures().field_presence == 3 and not fd.is_repeated
                         and fd.containing_oneof is None) else 0
        default = 'default=' + f.default_value if f.HasField('default_value') else '-'
        out.append(f'{pad}  resolved {1 if fd.has_presence else 0} '
                   f'{1 if fd.is_packed else 0} {fd.type} {utf8} {required} {default}')


def dump_message_def(out, msg, indent, runtime=None):
    # runtime: message descriptor of the pool for an editions file (resolved features).
    pad = ' ' * indent
    out.append(f'{pad}message {msg.name}')
    for f in msg.field:
        dump_field(out, pad + '  ', f, runtime.fields_by_name[f.name] if runtime else None)
    for f in msg.extension:
        dump_field(out, pad + '  ', f, runtime.extensions_by_name[f.name] if runtime else None,
                   'extension')
    for nested in msg.nested_type:
        dump_message_def(out, nested, indent + 2,
                         runtime.nested_types_by_name[nested.name] if runtime else None)
    for nested in msg.enum_type:
        dump_enum_def(out, nested, indent + 2,
                      runtime.enum_types_by_name[nested.name] if runtime else None)
    for o in msg.oneof_decl:
        out.append(f'{pad}  oneof {o.name}')
    for r in msg.reserved_range:
        out.append(f'{pad}  reserved_range {r.start} {r.end}')
    for n in msg.reserved_name:
        out.append(f'{pad}  reserved_name {n}')
    for r in msg.extension_range:
        out.append(f'{pad}  extension_range {r.start} {r.end}')
    if msg.options.map_entry:
        out.append(f'{pad}  map_entry')


def dump_enum_def(out, enum, indent, runtime=None):
    pad = ' ' * indent
    out.append(f'{pad}enum {enum.name}')
    if runtime is not None and runtime.is_closed:
        out.append(f'{pad}  closed')
    for v in enum.value:
        out.append(f'{pad}  value {v.name} {v.number}')
    for r in enum.reserved_range:
        out.append(f'{pad}  reserved_range {r.start} {r.end}')
    for n in enum.reserved_name:
        out.append(f'{pad}  reserved_name {n}')


EDITIONS = {1000: '2023', 1001: '2024'}


def runtime_file(fds):
    # Pool descriptor of the last file, for the features in effect.
    pool = descriptor_pool.DescriptorPool()
    for fdp in fds.file:
        pool.Add(fdp)
    return pool.FindFileByName(fds.file[-1].name)


def dump_file(fdp, runtime=None):
    out = [f'package {fdp.package}', f'syntax {fdp.syntax or "proto2"}']
    if fdp.syntax == 'editions':
        out.append(f'edition {EDITIONS.get(fdp.edition, fdp.edition)}')
    elif fdp.syntax == 'proto3':
        # proto3 has no resolved line: its rules are fixed.
        runtime = None
    for m in fdp.message_type:
        dump_message_def(out, m, 0, runtime.message_types_by_name[m.name] if runtime else None)
    for e in fdp.enum_type:
        dump_enum_def(out, e, 0, runtime.enum_types_by_name[e.name] if runtime else None)
    for f in fdp.extension:
        dump_field(out, '', f, runtime.extensions_by_name[f.name] if runtime else None, 'extension')
    for s in fdp.service:
        out.append(f'service {s.name}')
        for m in s.method:
            out.append(f'  method {m.name} {m.input_type} {m.output_type} '
                       f'{1 if m.client_streaming else 0} {1 if m.server_streaming else 0}')
    return '\n'.join(out) + '\n'


def in_parser_scope(fds):
    # Supported: proto2, proto3 and editions 2023/2024 files and their imports, no MessageSet.
    # Option-only imports (edition 2024) are not loaded, so they are not checked.
    by_name = {fdp.name: fdp for fdp in fds.file}
    used, todo = set(), [fds.file[-1].name]
    while todo:
        name = todo.pop()
        if name not in used and name in by_name:
            used.add(name)
            todo.extend(by_name[name].dependency)
    for fdp in fds.file:
        if fdp.name not in used:
            continue
        if fdp.syntax not in ('', 'proto2', 'proto3', 'editions'):
            return False
        if fdp.syntax == 'editions' and fdp.edition not in EDITIONS:
            return False
        stack = list(fdp.message_type)
        while stack:
            m = stack.pop()
            if m.options.message_set_wire_format:
                return False
            stack.extend(m.nested_type)
    return True


def write_text(path, text):
    with open(path, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write(text)


def cmd_descriptors(module_dir):
    ref = os.path.join(module_dir, 'reference')
    root = os.path.join(ref, 'golden', 'descriptors')
    # Import roots of each file, as given to protoc by gen_descriptors.py.
    with open(os.path.join(root, 'report.json'), encoding='utf-8') as fh:
        report = json.load(fh)
    roots = {}
    for r in report:
        roots.setdefault(r['corpus'], set()).add(r['root'])
    own = {(r['corpus'], r['file']): r['root'] for r in report}
    cases = []
    for d, _, files in os.walk(root):
        for f in sorted(files):
            if not f.endswith('.desc.binpb'):
                continue
            path = os.path.join(d, f)
            fds = descriptor_pb2.FileDescriptorSet()
            with open(path, 'rb') as fh:
                fds.ParseFromString(fh.read())
            main = fds.file[-1]
            txt = path[: -len('.binpb')] + '.txt'
            runtime = None
            if main.syntax != 'proto3':
                try:
                    runtime = runtime_file(fds)
                except Exception:
                    runtime = None
            write_text(txt, dump_file(main, runtime))
            rel = os.path.relpath(path, root).replace(os.sep, '/')
            proto = 'reference/protos/' + rel[: -len('.desc.binpb')] + '.proto'
            golden = 'reference/golden/descriptors/' + rel[: -len('.binpb')] + '.txt'
            corpus, file = rel.split('/', 1)
            file = file[: -len('.desc.binpb')] + '.proto'
            mine = own.get((corpus, file), '')
            incs = [mine] + sorted(x for x in roots.get(corpus, set()) if x != mine)
            paths = ';'.join('reference/protos/' + corpus + ('/' + x if x else '') for x in incs)
            cases.append(f'{1 if in_parser_scope(fds) else 0}\t{proto}\t{golden}\t{paths}')
    cases.sort(key=lambda c: c.split('\t')[1])
    write_text(os.path.join(ref, 'golden', 'parser_cases.tsv'), '\n'.join(cases) + '\n')
    print(len(cases), 'cases,', sum(c.startswith('1') for c in cases), 'in scope')


# ---------------------------------------------------------------- formats
def cmd_formats(module_dir, protoc):
    # Text format and JSON of every golden record, as the reference runtime prints them.
    from google.protobuf import json_format, text_format
    from google.protobuf.internal.decoder import _DecodeVarint32
    data = os.path.join(module_dir, 'tests', 'data')
    golden = os.path.join(data, 'golden')
    for stem in ('sensor_stream', 'editions_2023', 'editions_2024', 'json_types', 'legacy_proto2'):
        format_goldens(protoc, data, golden, stem, json_format, text_format, _DecodeVarint32)
    print('formats written')


def format_goldens(protoc, data, golden, stem, json_format, text_format, _DecodeVarint32):
    with tempfile.TemporaryDirectory() as tmp:
        desc = os.path.join(tmp, 'set.binpb')
        subprocess.run([protoc, '-I' + data, '--include_imports', '--descriptor_set_out=' + desc,
                        stem + '.proto'], check=True)
        fds = descriptor_pb2.FileDescriptorSet()
        with open(desc, 'rb') as fh:
            fds.ParseFromString(fh.read())
    pool = descriptor_pool.DescriptorPool()
    for f in fds.file:
        pool.Add(f)
    for name in sorted(os.listdir(golden)):
        if not name.endswith('.binpb') or not name.startswith(stem + '.'):
            continue
        full = name[len(stem + '.'):-len('.binpb')]
        cls = message_factory.GetMessageClass(pool.FindMessageTypeByName(full))
        with open(os.path.join(golden, name), 'rb') as fh:
            raw = fh.read()
        texts, jsons, raws = [], [], []
        pos = 0
        while pos < len(raw):
            size, pos = _DecodeVarint32(raw, pos)
            msg = cls()
            msg.ParseFromString(raw[pos:pos + size])
            # Schema-less view of the same bytes, as the reference compiler prints it.
            raws.append(subprocess.run([protoc, '--decode_raw'], input=raw[pos:pos + size],
                                       capture_output=True, check=True).stdout.decode('utf-8'))
            pos += size
            texts.append(text_format.MessageToString(msg, as_utf8=True))
            jsons.append(json.dumps(json_format.MessageToDict(msg), ensure_ascii=False))
        base = os.path.join(golden, name[:-len('.binpb')])
        write_text(base + '.text.txt', '===\n'.join(texts))
        write_text(base + '.json.txt', '\n'.join(jsons) + '\n')
        write_text(base + '.raw.txt', '===\n'.join(r.replace('\r\n', '\n') for r in raws))


# ---------------------------------------------------------------- messages
def float32(v):
    return struct.unpack('<f', struct.pack('<f', v))[0]


INT_RANGES = {
    FD.TYPE_INT32: (-2**31, 2**31 - 1), FD.TYPE_SINT32: (-2**31, 2**31 - 1),
    FD.TYPE_SFIXED32: (-2**31, 2**31 - 1), FD.TYPE_INT64: (-2**63, 2**63 - 1),
    FD.TYPE_SINT64: (-2**63, 2**63 - 1), FD.TYPE_SFIXED64: (-2**63, 2**63 - 1),
    FD.TYPE_UINT32: (0, 2**32 - 1), FD.TYPE_FIXED32: (0, 2**32 - 1),
    FD.TYPE_UINT64: (0, 2**64 - 1), FD.TYPE_FIXED64: (0, 2**64 - 1),
}
STRINGS = ['', 'a', 'héllo', '日本', '\U0001F642', 'line\nbreak', 'tab\tquote"']


def random_scalar(field, rnd):
    t = field.type
    if t in INT_RANGES:
        lo, hi = INT_RANGES[t]
        return rnd.choice([0, 1, lo, hi, max(lo, -1), rnd.randint(-1000 if lo < 0 else 0, 1000),
                           rnd.randint(lo, hi)])
    if t == FD.TYPE_BOOL:
        return rnd.random() < 0.5
    if t in (FD.TYPE_FLOAT, FD.TYPE_DOUBLE):
        v = rnd.choice([0.0, -0.0, 1.5, -2.25, float('inf'), float('-inf'), float('nan'),
                        rnd.uniform(-1e6, 1e6), 1e-30, 1e30])
        return float32(v) if t == FD.TYPE_FLOAT else v
    if t == FD.TYPE_STRING:
        if rnd.random() < 0.3:
            return ''.join(rnd.choice('abcxyz_0123') for _ in range(rnd.randint(1, 20)))
        return rnd.choice(STRINGS)
    if t == FD.TYPE_BYTES:
        return bytes(rnd.randint(0, 255) for _ in range(rnd.randint(0, 10)))
    if t == FD.TYPE_ENUM:
        numbers = [v.number for v in field.enum_type.values]
        # Closed enums only hold their own values.
        return rnd.choice(numbers + ([] if field.enum_type.is_closed else [77]))
    raise ValueError(t)


# Values valid in JSON for the well-known types (json_types.proto only).
JSON_SAFE = {'on': False}
FIELD_PATHS = ['a', 'a.b', 'sample_rate', 'diagnostics.drift', 'x_y.z']


def populate_json_safe(msg, rnd, depth):
    # Fills a well-known message with values its JSON form can hold; False for other types.
    name = msg.DESCRIPTOR.full_name
    if name == 'google.protobuf.Timestamp':
        msg.seconds = rnd.randint(-62135596800, 253402300799)
        msg.nanos = rnd.choice([0, 1000, 5000000, rnd.randint(0, 999999999)])
    elif name == 'google.protobuf.Duration':
        seconds = rnd.randint(-315576000000, 315576000000)
        nanos = rnd.choice([0, 250000000, rnd.randint(0, 999999999)])
        msg.seconds = seconds
        msg.nanos = -nanos if seconds < 0 else nanos
    elif name == 'google.protobuf.FieldMask':
        msg.paths.extend(rnd.sample(FIELD_PATHS, rnd.randint(0, 3)))
    elif name == 'google.protobuf.Value':
        kind = rnd.choice(['null', 'number', 'string', 'bool', 'struct', 'list'] if depth < 3
                          else ['null', 'number', 'string', 'bool'])
        if kind == 'null':
            msg.null_value = 0
        elif kind == 'number':
            msg.number_value = rnd.choice([0.0, -1.5, 3.0, 1e-7, 123456789.25, rnd.uniform(-1e9, 1e9)])
        elif kind == 'string':
            msg.string_value = rnd.choice(STRINGS)
        elif kind == 'bool':
            msg.bool_value = rnd.random() < 0.5
        elif kind == 'struct':
            populate_json_safe(msg.struct_value, rnd, depth + 1)
        else:
            populate_json_safe(msg.list_value, rnd, depth + 1)
    elif name == 'google.protobuf.Struct':
        for _ in range(rnd.randint(0, 3)):
            populate_json_safe(msg.fields[rnd.choice(STRINGS + ['k1', 'k2'])], rnd, depth + 1)
    elif name == 'google.protobuf.ListValue':
        for _ in range(rnd.randint(0, 3)):
            populate_json_safe(msg.values.add(), rnd, depth + 1)
    elif name in ('google.protobuf.DoubleValue', 'google.protobuf.FloatValue'):
        msg.value = rnd.choice([0.0, 2.5, -0.125, float('inf'), float('nan')])
    else:
        return False
    return True


def populate(msg, rnd, depth):
    if JSON_SAFE['on'] and populate_json_safe(msg, rnd, depth):
        return
    d = msg.DESCRIPTOR
    chosen = {}
    for o in d.oneofs:
        if not getattr(o, 'is_synthetic', False) and not (len(o.fields) == 1 and o.fields[0].has_presence and o.name.startswith('_')):
            members = list(o.fields)
            chosen[o.name] = rnd.choice(members + [None])
    for f in d.fields:
        o = f.containing_oneof
        o = o if (o is not None and o.name in chosen) else None
        if o is not None and chosen[o.name] is not f:
            continue
        is_message = f.type in (FD.TYPE_MESSAGE, FD.TYPE_GROUP)
        if f.message_type is not None and f.message_type.GetOptions().map_entry:
            key_f = f.message_type.fields_by_name['key']
            val_f = f.message_type.fields_by_name['value']
            container = getattr(msg, f.name)
            for _ in range(rnd.randint(0, 3)):
                key = random_scalar(key_f, rnd)
                if val_f.type == FD.TYPE_MESSAGE:
                    if depth < 3:
                        populate(container[key], rnd, depth + 1)
                else:
                    container[key] = random_scalar(val_f, rnd)
            continue
        if f.is_repeated:
            container = getattr(msg, f.name)
            for _ in range(rnd.randint(0, 3)):
                if is_message:
                    if depth < 3:
                        populate(container.add(), rnd, depth + 1)
                else:
                    container.append(random_scalar(f, rnd))
            continue
        if is_message:
            if depth < 3 and (rnd.random() < 0.7 or o is not None):
                sub = getattr(msg, f.name)
                sub.SetInParent()
                populate(sub, rnd, depth + 1)
            continue
        required = f._GetFeatures().field_presence == 3
        if f.has_presence and rnd.random() < 0.4 and o is None and not required:
            continue
        if rnd.random() < 0.15 and o is None and not required:
            continue
        setattr(msg, f.name, random_scalar(f, rnd))


def scalar_text(field, v):
    t = field.type
    if t == FD.TYPE_DOUBLE:
        return 'd:%016x' % struct.unpack('<Q', struct.pack('<d', v))[0]
    if t == FD.TYPE_FLOAT:
        return 'f:%08x' % struct.unpack('<I', struct.pack('<f', v))[0]
    if t == FD.TYPE_BOOL:
        return 'true' if v else 'false'
    if t == FD.TYPE_STRING:
        return 'x:' + v.encode('utf-8').hex()
    if t == FD.TYPE_BYTES:
        return 'x:' + bytes(v).hex()
    return str(int(v))


def dump_row(msg, indent=0):
    pad = ' ' * indent
    out = []
    d = msg.DESCRIPTOR
    for f in sorted(d.fields, key=lambda f: f.number):
        if f.message_type is not None and f.message_type.GetOptions().map_entry:
            key_f = f.message_type.fields_by_name['key']
            val_f = f.message_type.fields_by_name['value']
            entries = []
            for key, value in getattr(msg, f.name).items():
                lines = [f'{pad}{f.name}[] {{', f'{pad}  key: {scalar_text(key_f, key)}']
                if val_f.type == FD.TYPE_MESSAGE:
                    lines.append(f'{pad}  value {{')
                    lines.extend(dump_row(value, indent + 4))
                    lines.append(f'{pad}  }}')
                else:
                    lines.append(f'{pad}  value: {scalar_text(val_f, value)}')
                lines.append(f'{pad}}}')
                entries.append('\n'.join(lines) + '\n')
            for e in sorted(entries):
                out.extend(e.rstrip('\n').split('\n'))
            continue
        if f.is_repeated:
            for i, v in enumerate(getattr(msg, f.name)):
                if f.type in (FD.TYPE_MESSAGE, FD.TYPE_GROUP):
                    out.append(f'{pad}{f.name}[{i}] {{')
                    out.extend(dump_row(v, indent + 2))
                    out.append(f'{pad}}}')
                else:
                    out.append(f'{pad}{f.name}[{i}]: {scalar_text(f, v)}')
            continue
        if f.type in (FD.TYPE_MESSAGE, FD.TYPE_GROUP):
            if msg.HasField(f.name):
                out.append(f'{pad}{f.name} {{')
                out.extend(dump_row(getattr(msg, f.name), indent + 2))
                out.append(f'{pad}}}')
            continue
        if f.has_presence and not msg.HasField(f.name):
            continue
        out.append(f'{pad}{f.name}: {scalar_text(f, getattr(msg, f.name))}')
    return out


def cmd_messages(module_dir, protoc, count):
    data = os.path.join(module_dir, 'tests', 'data')
    out_root = os.path.join(data, 'golden')
    os.makedirs(out_root, exist_ok=True)
    for name in sorted(os.listdir(data)):
        if not name.endswith('.proto'):
            continue
        stem = name[: -len('.proto')]
        with tempfile.TemporaryDirectory() as tmp:
            desc = os.path.join(tmp, 'set.binpb')
            subprocess.run([protoc, '-I' + data, '--include_imports',
                            '--descriptor_set_out=' + desc, name], check=True)
            fds = descriptor_pb2.FileDescriptorSet()
            with open(desc, 'rb') as fh:
                fds.ParseFromString(fh.read())
        write_text(os.path.join(out_root, stem + '.desc.txt'),
                   dump_file(fds.file[-1], runtime_file(fds)))
        pool = descriptor_pool.DescriptorPool()
        for fdp in fds.file:
            pool.Add(fdp)
        fdesc = pool.FindFileByName(name)
        rnd = random.Random(20261112)
        JSON_SAFE['on'] = stem == 'json_types'
        names = []
        stack = list(fdesc.message_types_by_name.values())
        while stack:
            m = stack.pop(0)
            if not m.GetOptions().map_entry:
                names.append(m.full_name)
            stack.extend(m.nested_types)
        for full in names:
            cls = message_factory.GetMessageClass(pool.FindMessageTypeByName(full))
            stream = bytearray()
            dumps = []
            for _ in range(count):
                msg = cls()
                populate(msg, rnd, 0)
                raw = msg.SerializeToString(deterministic=True)
                again = cls()
                again.ParseFromString(raw)
                stream += _VarintBytes(len(raw)) + raw
                dumps.append('\n'.join(dump_row(again)))
            base = os.path.join(out_root, stem + '.' + full)
            with open(base + '.binpb', 'wb') as fh:
                fh.write(bytes(stream))
            write_text(base + '.dump.txt', '\n---\n'.join(dumps) + '\n')
            print(full, len(stream), 'bytes')


if __name__ == '__main__':
    if sys.argv[1] == 'descriptors':
        cmd_descriptors(sys.argv[2])
    elif sys.argv[1] == 'messages':
        cmd_messages(sys.argv[2], sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else 25)
    elif sys.argv[1] == 'formats':
        cmd_formats(sys.argv[2], sys.argv[3])
    else:
        raise SystemExit('unknown command')
