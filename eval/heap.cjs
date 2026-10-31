// node eval/heap.cjs <kz-slicer.js> [rounds]: one module (one worker), the 9 models x 3 settings x supports
// off/on in turn, `rounds` times; prints the wasm heap size after each round. The heap never shrinks,
// so it shows the high-water mark a pricer worker would be holding.
const fs = require('fs'), path = require('path');
let mem = null; const Orig = WebAssembly.Memory;
WebAssembly.Memory = function (d) { const m = new Orig(d); mem = mem || m; return m; };
WebAssembly.Memory.prototype = Orig.prototype;
const M = '/home/user/kz-rewrite/eval/models/', CH = [[3, 40], [2, 15], [4, 80]];
const ov = (w, i, s) => [['printable_area','0x0,1000x0,1000x1000,0x1000'],['printable_height',1000],['wall_loops',w],['sparse_infill_density',i+'%'],['enable_support',s],
  ...(s ? [['support_type','tree(auto)'],['support_style','organic'],['support_threshold_angle',35],['support_interface_top_layers',8],['support_interface_bottom_layers',8]] : [])].map(([k,v])=>`${k}\t${v}\n`).join('');
(async () => {
  const F = require(path.resolve(process.argv[2])); const m = await (F.default || F)({ print() {}, printErr() {} });
  m.FS.mkdir('/p'); m.FS.writeFile('/p/k.json', fs.readFileSync('/home/user/kz-rewrite/eval/profiles/karaza.json'));
  const models = process.env.MODELS ? process.env.MODELS.split(',') : [...fs.readdirSync(M).filter(f => f.endsWith('.stl')).map(f => M + f), '/home/user/karaza-slicer/eval/models/benchy.stl'];
  const T = Date.now(); const out = [`start ${mem.buffer.byteLength >> 20}`];
  for (let r = 0; r < (+process.argv[3] || 4); r++) {
    for (const mf of models) { m.FS.writeFile('/in.stl', fs.readFileSync(mf));
      for (const [w, i] of CH) for (const s of [0, 1]) m.ccall('kz_slice', 'string', ['string','string','string','string'], ['/in.stl', '/p/k.json', ov(w, i, s), '']); }
    out.push(`r${r + 1} ${mem.buffer.byteLength >> 20} (${((Date.now() - T) / 1000) | 0}s)`);
  }
  console.log(out.join('  ') + ' MB'); process.exit(0);
})();
