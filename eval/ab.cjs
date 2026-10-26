// node eval/ab.cjs A.js B.js [reps]: same 54 gate cases, A and B alternated per case, prints totals and per-model ratio
const fs = require('fs'), path = require('path');
const M = '/home/user/kz-rewrite/eval/models/', P = '/home/user/kz-rewrite/eval/profiles/karaza.json';
const CH = [[3, 40], [2, 15], [4, 80]];
const ov = (w, i, s) => [['printable_area','0x0,1000x0,1000x1000,0x1000'],['printable_height',1000],['wall_loops',w],['sparse_infill_density',i+'%'],['enable_support',s],
  ...(s ? [['support_type','tree(auto)'],['support_style','organic'],['support_threshold_angle',35],['support_interface_top_layers',8],['support_interface_bottom_layers',8]] : [])].map(([k,v])=>`${k}\t${v}\n`).join('');
(async () => {
  const load = async (f) => { const F = require(path.resolve(f)); const m = await (F.default || F)({ print() {}, printErr() {} }); m.FS.mkdir('/p'); m.FS.writeFile('/p/k.json', fs.readFileSync(P)); return m; };
  const mods = [await load(process.argv[2]), await load(process.argv[3])];
  const reps = +process.argv[4] || 2, tot = [0, 0], per = {};
  const models = fs.readdirSync(M).filter(f => f.endsWith('.stl')).map(f => M + f); models.push('/home/user/karaza-slicer/eval/models/benchy.stl');
  for (let r = 0; r < reps; r++) for (const mf of models) {
    const buf = fs.readFileSync(mf); const name = path.basename(mf, '.stl');
    for (const [w, i] of CH) for (const s of [0, 1]) {
      const order = (r + w + s) % 2 ? [0, 1] : [1, 0];
      for (const x of order) {
        const m = mods[x]; m.FS.writeFile('/in.stl', buf);
        const t = performance.now(); m.ccall('kz_slice', 'string', ['string','string','string','string'], ['/in.stl', '/p/k.json', ov(w, i, s), '']); const d = performance.now() - t;
        tot[x] += d; (per[name] ??= [0, 0])[x] += d;
      }
    }
  }
  console.log(`A ${tot[0]|0} ms  B ${tot[1]|0} ms  B/A ${(tot[1]/tot[0]).toFixed(3)}`);
  console.log(Object.entries(per).map(([k, [a, b]]) => `${k} ${(b/a).toFixed(2)}`).join('  '));
  process.exit(0);
})();
