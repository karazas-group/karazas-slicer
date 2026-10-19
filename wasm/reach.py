#!/usr/bin/env python3
"""Object-level reachability: start from root objects, repeatedly pull in every object that
defines (strong/weak) a symbol some already-pulled object references. Mimics a static-library link.
usage: reach.py <root.o>... -- <candidate objs dir>..."""
import subprocess, sys, os, collections

NM = os.path.join(os.environ.get('ORCA_WASM', os.path.expanduser('~/orca-wasm')), 'emsdk/upstream/bin/llvm-nm')
args = sys.argv[1:]
sep = args.index('--')
roots, dirs = args[:sep], args[sep + 1:]

def syms(path):
    out = subprocess.run([NM, '-P', path], capture_output=True, text=True).stdout
    defs, undefs = set(), set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 2: continue
        name, t = parts[0], parts[1]
        if t == 'U': undefs.add(name)
        elif t in 'TDBRWVC': defs.add(name)
    return defs, undefs

cands = []
for d in dirs:
    for f in sorted(os.listdir(d)):
        if f.endswith('.o'): cands.append(os.path.join(d, f))
info = {p: syms(p) for p in set(cands) | set(roots)}
strong_def = collections.defaultdict(list)
for p, (defs, _) in info.items():
    for s in defs: strong_def[s].append(p)

pulled, why = set(roots), {}
queue = list(roots)
while queue:
    p = queue.pop()
    for s in info[p][1]:
        for q in strong_def.get(s, []):
            if q not in pulled:
                pulled.add(q); why[q] = (p, s); queue.append(q)

all_defs = set().union(*(info[p][0] for p in pulled))
missing = sorted(set().union(*(info[p][1] for p in pulled)) - all_defs)
print('PULLED', len(pulled))
for p in sorted(pulled):
    src = why.get(p)
    print('  ', os.path.basename(p), '<-', os.path.basename(src[0]) if src else 'ROOT')
print('UNPULLED')
for p in sorted(set(cands) - pulled): print('  ', os.path.basename(p))
print('MISSING', len(missing))
for s in missing:
    print('  ', s)
