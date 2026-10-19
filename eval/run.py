# Slice one model with the OrcaSlicer CLI and print grams / time.
# usage: run.py <printer p2s|s1> <model.stl> <tag> [key=value ...]   (overrides go into the process profile)
import json, os, subprocess, sys, zipfile, re, time, shutil
here = os.path.dirname(os.path.abspath(__file__))
printer, model, tag, *kv = sys.argv[1:]
proc = json.load(open(f'{here}/profiles/{printer}-process.json'))
for item in kv:
    k, v = item.split('=', 1)
    old = proc.get(k)
    proc[k] = [v] * len(old) if isinstance(old, list) else v
work = f'{here}/work/{tag}'
shutil.rmtree(work, ignore_errors=True); os.makedirs(work)
json.dump(proc, open(f'{work}/process.json', 'w'))
cmd = [f'{here}/app/AppRun', '--load-settings', f'{here}/profiles/{printer}-machine.json;{work}/process.json',
       '--load-filaments', f'{here}/profiles/{printer}-pla.json', '--slice', '0', '--arrange', '1', '--orient', '0',
       '--outputdir', work, '--export-3mf', 'out.3mf', os.path.abspath(model)]
t = time.time()
r = subprocess.run(cmd, capture_output=True, text=True, timeout=900)
ms = round((time.time() - t) * 1000)
res = {}
try:
    with zipfile.ZipFile(f'{work}/out.3mf') as z:
        g = z.read('Metadata/plate_1.gcode').decode(errors='replace')
    for k, pat in [('grams', r'filament used \[g\] = ([\d.]+)'), ('mm', r'filament used \[mm\] = ([\d.]+)'), ('time', r'model printing time: ([^;\n]+)')]:
        m = re.search(pat, g)
        if m: res[k] = m.group(1).strip()
except Exception as e:
    res['error'] = str(e)
    res['log'] = (r.stdout + r.stderr)[-1500:]
res['ms'] = ms
print(json.dumps(res))
