# Resolve an Orca system preset's "inherits" chain into one flat JSON the CLI can load.
# usage: flatten.py <profiles dir> <vendor> <type> <name> <out.json>
import json, os, sys
root, vendor, kind, name, out = sys.argv[1:6]
index = {}
for v in (vendor, 'OrcaFilamentLibrary'):
    d = os.path.join(root, v, kind)
    for dp, _, fs in os.walk(d):
        for f in fs:
            if f.endswith('.json'):
                try: j = json.load(open(os.path.join(dp, f)))
                except Exception: continue
                index.setdefault(j.get('name'), j)
def resolve(n):
    j = index[n]
    base = resolve(j['inherits']) if j.get('inherits') else {}
    base.update(j)
    return base
r = resolve(name)
r.pop('inherits', None)
r['from'] = 'User'
r['name'] = name
json.dump(r, open(out, 'w'), indent=1)
print(out, len(r), 'keys')
