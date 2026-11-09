// node eval/laps.cjs kz-slicer.js [reps]: the 54 gate cases with kz::lap
// markers, summed per phase, split by supports off/on. The first rep warms
// the module up and isn't counted (unless reps is 1).
const fs = require('fs'), path = require('path');
const M = __dirname + '/models/', P = __dirname + '/profiles/karaza.json';
const CH = [[3, 40], [2, 15], [4, 80]];
const ov = (w, i, s) => [['printable_area','0x0,1000x0,1000x1000,0x1000'],['printable_height',1000],['wall_loops',w],['sparse_infill_density',i+'%'],['enable_support',s],
  ...(s ? [['support_type', 'normal(auto)'], ['support_style', 'grid'],['support_threshold_angle',35],['support_interface_top_layers',8],['support_interface_bottom_layers',8]] : [])].map(([k,v])=>`${k}\t${v}\n`).join('');
(async () => {
  let laps = [];
  const F = require(path.resolve(process.argv[2]));
  const m = await (F.default || F)({ print() {}, printErr(t) { const x = /^T (.+) (\d+)$/.exec(t); if (x) laps.push([x[1], +x[2]]); } });
  m.FS.mkdir('/p'); m.FS.writeFile('/p/k.json', fs.readFileSync(P));
  const reps = +process.argv[3] || 2;
  const models = fs.readdirSync(M).filter(f => f.endsWith('.stl')).map(f => M + f); models.push(process.env.BENCHY || __dirname + '/models/benchy.stl');
  const agg = [{}, {}], per = {};
  for (let r = 0; r < reps; r++) for (const mf of models) {
    const buf = fs.readFileSync(mf), name = path.basename(mf, '.stl');
    for (const [w, i] of CH) for (const s of [0, 1]) {
      m.FS.writeFile('/in.stl', buf); laps = [];
      m.ccall('kz_slice', 'string', ['string','string','string','string'], ['/in.stl', '/p/k.json', ov(w, i, s), '']);
      if (reps > 1 && r === 0) continue;
      // a lap is printed with the name of the step that starts next; shift so each ms belongs to the step it measured
      for (let k = 1; k < laps.length; k++) {
        const ph = laps[k - 1][0], ms = laps[k][1];
        agg[s][ph] = (agg[s][ph] || 0) + ms;
        const key = name + (s ? '+sup' : ''); (per[key] ??= {})[ph] = (per[key][ph] || 0) + ms;
      }
    }
  }
  const n = reps > 1 ? reps - 1 : 1;
  for (const s of [0, 1]) {
    const tot = Object.values(agg[s]).reduce((a, b) => a + b, 0);
    console.log(`\nsupports ${s ? 'on' : 'off'}: ${(tot / n) | 0} ms per rep`);
    for (const [ph, ms] of Object.entries(agg[s]).sort((a, b) => b[1] - a[1])) console.log(`  ${(ms / n | 0).toString().padStart(6)}  ${(100 * ms / tot).toFixed(1).padStart(5)}%  ${ph}`);
  }
  if (process.env.PER) for (const [k, o] of Object.entries(per)) console.log(k, JSON.stringify(Object.fromEntries(Object.entries(o).map(([a, b]) => [a, b / n | 0]))));
  process.exit(0);
})();
