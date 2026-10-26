// node eval/gate.cjs <kz-slicer.js> [--write]
// The rewrite's gate: every model in eval/models (plus Benchy) with the
// pricer's settings file (eval/profiles/karaza.json, a copy of the
// website's assets/kz/slicer/karaza.json) and the overrides the pricer sends
// (src/entries/slicer.ts there): 3 wall/infill choices, supports off and on.
// filament_mm is compared at full precision with eval/golden-mm.txt, so a
// stage passes only if the plastic is bit-identical. Exit 0 only if every
// case matches. --write records a new golden file from a trusted build.
// benchy.stl is not in the repo (11 MB): put it in eval/models/ or set BENCHY.
const fs = require('fs'), path = require('path');
const E = __dirname + '/', M = E + 'models/', G = E + 'golden-mm.txt';
const CHOICES = [[3, 40], [2, 15], [4, 80]];
const overrides = (walls, infill, sup) => [
  ['printable_area', '0x0,1000x0,1000x1000,0x1000'], ['printable_height', 1000],
  ['wall_loops', walls], ['sparse_infill_density', infill + '%'], ['enable_support', sup],
  ...(sup ? [['support_type', 'tree(auto)'], ['support_style', 'organic'], ['support_threshold_angle', 35],
    ['support_interface_top_layers', 8], ['support_interface_bottom_layers', 8]] : []),
].map(([k, v]) => `${k}\t${v}\n`).join('');
(async () => {
  const write = process.argv.includes('--write');
  const F = require(path.resolve(process.argv[2])); const m = await (F.default || F)({ print() {}, printErr() {} });
  m.FS.mkdir('/p'); m.FS.writeFile('/p/karaza.json', fs.readFileSync(E + 'profiles/karaza.json'));
  const golden = write ? new Map() : new Map(fs.readFileSync(G, 'utf8').trim().split('\n').map((l) => { const i = l.lastIndexOf(' '); return [l.slice(0, i), l.slice(i + 1)]; }));
  const models = fs.readdirSync(M).filter((f) => f.endsWith('.stl')).map((f) => f.slice(0, -4));
  if (!models.includes('benchy') && process.env.BENCHY) models.push('benchy');
  let bad = 0, n = 0, tot = 0; const out = [];
  for (const name of models.sort()) {
    m.FS.writeFile('/in.stl', fs.readFileSync(name === 'benchy' && process.env.BENCHY ? process.env.BENCHY : M + name + '.stl'));
    for (const [w, inf] of CHOICES) for (const sup of [0, 1]) {
      const t = Date.now();
      const r = JSON.parse(m.ccall('kz_slice', 'string', ['string', 'string', 'string', 'string'], ['/in.stl', '/p/karaza.json', overrides(w, inf, sup), '']));
      tot += Date.now() - t; n++;
      const key = `${name} w${w} i${inf} s${sup}`, got = r.rc === 0 ? String(r.filament_mm) : 'rc' + r.rc;
      out.push(`${key} ${got}`);
      if (!write && got !== golden.get(key)) { bad++; console.log(`DIFF ${key} got ${got} want ${golden.get(key)}`); }
    }
  }
  if (write) { fs.writeFileSync(G, out.join('\n') + '\n'); console.log(`wrote ${n} cases to ${G}, ${tot} ms`); process.exit(0); }
  console.log(`${n - bad}/${n} identical, ${tot} ms`);
  process.exit(bad || n !== golden.size ? 1 : 0);
})();
