// node eval/gcode.cjs <kz-slicer.js from link_check.sh> <model.stl> <out.gcode> [walls infill supports]
// Slices one model with the validation build (G-code export linked back in)
// using the pricer's settings file and overrides, and writes the G-code, so
// eval/supports_view.py can draw it next to native Orca's.
const fs = require('fs'), path = require('path');
const [js, model, out, walls = 3, infill = 40, sup = 1] = process.argv.slice(2);
const ov = [['printable_area', '0x0,1000x0,1000x1000,0x1000'], ['printable_height', 1000], ['wall_loops', walls],
  ['sparse_infill_density', infill + '%'], ['enable_support', sup],
  ...(+sup ? [['support_type', 'tree(auto)'], ['support_style', 'organic'], ['support_threshold_angle', 35],
    ['support_interface_top_layers', 8], ['support_interface_bottom_layers', 8]] : [])]
  .map(([k, v]) => `${k}\t${v}\n`).join('');
(async () => {
  const F = require(path.resolve(js)); const m = await (F.default || F)({ print() {}, printErr() {} });
  m.FS.mkdir('/p'); m.FS.writeFile('/p/karaza.json', fs.readFileSync(__dirname + '/profiles/karaza.json'));
  m.FS.writeFile('/in.stl', fs.readFileSync(model));
  const r = JSON.parse(m.ccall('kz_slice', 'string', ['string', 'string', 'string', 'string'], ['/in.stl', '/p/karaza.json', ov, '/o.gcode']));
  fs.writeFileSync(out, m.FS.readFile('/o.gcode'));
  console.log(JSON.stringify(r)); process.exit(r.rc ? 1 : 0);
})();
