// node eval/gate.cjs <kz-slicer.js>
// The rewrite's gate: the 28-case matrix (7 models x P2S/S1 x supports off/on)
// through a built kz-slicer.js, grams compared as strings with
// eval/golden-grams.txt. Exit 0 only if all 28 are identical. Grams come out
// of kz_slice rounded to 0.01 g, so equal here means equal to the hundredth;
// for a bit-level check of a stage see REWRITE.md (volume dump).
// benchy.stl is not in the repo (11 MB): put it in eval/models/ or set BENCHY.
const fs = require('fs'), path = require('path');
const E = __dirname + '/', M = E + 'models/';
const golden = new Map(fs.readFileSync(E + 'golden-grams.txt', 'utf8').trim().split('\n').map((l) => { const [n, c, g] = l.split(' '); return [n + ' ' + c, g]; }));
const ORG = 'enable_support\t1\nsupport_type\ttree(auto)\nsupport_style\torganic\nsupport_threshold_angle\t35\n';
(async () => {
  const F = require(path.resolve(process.argv[2])); const m = await (F.default || F)({ print() {}, printErr() {} });
  m.FS.mkdir('/p'); for (const pr of ['p2s', 's1']) for (const k of ['machine', 'process', 'pla']) m.FS.writeFile(`/p/${pr}-${k}.json`, fs.readFileSync(`${E}profiles/${pr}-${k}.json`));
  let bad = 0, n = 0, tot = 0;
  for (const name of ['cube20', 'cup', 'sphere40', 'invcone', 'mushroom', 'bracket', 'benchy']) {
    const file = name === 'benchy' && process.env.BENCHY ? process.env.BENCHY : M + name + '.stl';
    m.FS.writeFile('/in.stl', fs.readFileSync(file));
    for (const pr of ['p2s', 's1']) for (const sup of ['s0', 'org35']) {
      const t = Date.now();
      const r = JSON.parse(m.ccall('kz_slice', 'string', ['string', 'string', 'string', 'string'], ['/in.stl', `/p/${pr}-machine.json;/p/${pr}-process.json;/p/${pr}-pla.json`, sup === 'org35' ? ORG : '', '/o.gcode']));
      tot += Date.now() - t; n++;
      const key = `${name} ${pr}-${sup}`, got = String(r.filament_g), want = golden.get(key);
      if (got !== want) { bad++; console.log(`DIFF ${key} got ${got} want ${want}`); }
    }
  }
  console.log(`${n - bad}/${n} identical, ${tot} ms`);
  process.exit(bad || n !== golden.size ? 1 : 0);
})();
