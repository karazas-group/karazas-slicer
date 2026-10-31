# Slices one model with native OrcaSlicer 2.4.2 (eval/app, the AppImage
# extract) using the full profiles the pricer's karaza.json was cut from and
# the pricer's overrides, and writes the G-code. The reference picture for
# eval/supports_view.py.
# usage: native_gcode.py <profiles dir with p2s-{machine,process,pla}.json> <model.stl> <out.gcode> [walls infill supports]
import json, os, shutil, subprocess, sys, tempfile, zipfile
here = os.path.dirname(os.path.abspath(__file__))
prof, model, out, *rest = sys.argv[1:]
walls, infill, sup = (rest + ['3', '40', '1'][len(rest):])[:3]
proc = json.load(open(f'{prof}/p2s-process.json'))
over = {'wall_loops': walls, 'sparse_infill_density': infill + '%', 'enable_support': sup, 'brim_type': 'outer_only'}
if sup == '1':
    over.update({'support_type': 'tree(auto)', 'support_style': 'organic', 'support_threshold_angle': '35',
                 'support_interface_top_layers': '8', 'support_interface_bottom_layers': '8'})
for k, v in over.items():
    proc[k] = [v] * len(proc[k]) if isinstance(proc.get(k), list) else v
work = tempfile.mkdtemp()
json.dump(proc, open(f'{work}/process.json', 'w'))
cmd = [f'{here}/app/AppRun', '--load-settings', f'{prof}/p2s-machine.json;{work}/process.json',
       '--load-filaments', f'{prof}/p2s-pla.json', '--slice', '0', '--arrange', '1', '--orient', '0',
       '--outputdir', work, '--export-3mf', 'out.3mf', os.path.abspath(model)]
r = subprocess.run(cmd, capture_output=True, text=True, timeout=900)
try:
    with zipfile.ZipFile(f'{work}/out.3mf') as z:
        open(out, 'wb').write(z.read('Metadata/plate_1.gcode'))
except Exception as e:
    sys.exit(f'native slice failed: {e}\n{(r.stdout + r.stderr)[-1500:]}')
finally:
    shutil.rmtree(work, ignore_errors=True)
