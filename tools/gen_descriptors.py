# Generate one FileDescriptorSet (.binpb, imports included) per corpus .proto with protoc.
# Usage: python -I gen_descriptors.py <protobuf_reference dir> <protoc.exe>
import os, re, subprocess, sys, json

ref, protoc = sys.argv[1], sys.argv[2]
protos = os.path.join(ref, 'protos')
out_root = os.path.join(ref, 'golden', 'descriptors')
imp = re.compile(r'^\s*import\s+(?:public\s+|weak\s+|option\s+)?"([^"]+)"', re.M)
edition = re.compile(r'^\s*edition\s*=\s*"([^"]+)"', re.M)


def rel(p, base):
    return os.path.relpath(p, base).replace(chr(92), '/')


report = []
for corpus in sorted(os.listdir(protos)):
    cdir = os.path.join(protos, corpus)
    files = []
    for d, _, fs in os.walk(cdir):
        files += [rel(os.path.join(d, f), cdir) for f in fs if f.endswith('.proto')]
    texts = {f: open(os.path.join(cdir, f), encoding='utf-8').read() for f in files}
    imports = {f: imp.findall(t) for f, t in texts.items()}
    # A root is the prefix left when an import name is the tail of a corpus path.
    roots, own = set(), {}
    for f in files:
        for name in imports[f]:
            for p in files:
                if p == name or p.endswith('/' + name):
                    r = p[: len(p) - len(name)].rstrip('/')
                    roots.add(r)
                    own.setdefault(p, r)
    for f in files:
        r = own.get(f)
        if r is None:
            cands = [x for x in roots if x == '' or f.startswith(x + '/')]
            r = max(cands, key=len) if cands else os.path.dirname(f)
        name = f[len(r) + 1:] if r else f
        incs = [r] + sorted(x for x in roots if x != r)
        args = [protoc, '--include_imports', '--include_source_info']
        if edition.search(texts[f]) and edition.search(texts[f]).group(1) not in ('2023', '2024'):
            args.append('--experimental_editions')
        args += ['-I' + os.path.join(cdir, x) if x else '-I' + cdir for x in incs]
        out = os.path.join(out_root, corpus, os.path.splitext(f)[0] + '.desc.binpb')
        os.makedirs(os.path.dirname(out), exist_ok=True)
        args += ['--descriptor_set_out=' + out, name]
        res = subprocess.run(args, capture_output=True, text=True)
        ok = res.returncode == 0
        if not ok and os.path.exists(out):
            os.remove(out)
        report.append({'corpus': corpus, 'file': f, 'name': name, 'root': r, 'ok': ok,
                       'stderr': res.stderr.strip()})

with open(os.path.join(out_root, 'report.json'), 'w', encoding='utf-8') as fh:
    json.dump(report, fh, indent=1)
print('ok', sum(r['ok'] for r in report), 'failed', sum(not r['ok'] for r in report))
for r in report:
    if not r['ok']:
        print('FAIL', r['corpus'], r['file'], '|', r['stderr'].splitlines()[0][:200] if r['stderr'] else '')
