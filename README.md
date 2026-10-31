# Karaza slicer
test
OrcaSlicer's slicing core (libslic3r, v2.4.2) compiled to WebAssembly, so the
3D pricer can slice a customer's model in their browser with the same engine
and the same profiles the shop prints with. It replaced Kiri:Moto in the pricer
on 2026-10-20.

This repo started as the `karaza-slicer` branch of the website repo and moved
here on 2026-10-24 with its history. Nothing in it depends on the website.

The browser build is `kz-slicer.{js,wasm}` (factory `KzSlicer`). The website
serves it from `assets/kz/slicer/`; `wasm/link_web.sh` writes it.

## Licence

OrcaSlicer is AGPL-3.0, so this repo is too: see `LICENSE`. The website hands
`kz-slicer.wasm` to every visitor who uses the pricer, so they're entitled to
this source, including our patch and driver. Keeping this repo public and
linking to it from the site covers that. The licence can't be changed, since
it comes from OrcaSlicer (and PrusaSlicer and Slic3r before it).

## Where it stands

From the first spike (full write-up in `REPORT.md`):

- Unmodified libslic3r plus a 70-line driver links into one 8.4 MB `.wasm`
  (2.66 MB gzip, before any trimming).
- All 28 test cases slice. 24 give the same grams as the native OrcaSlicer
  2.4.2 CLI, to the hundredth of a gram, on the P2S and Ender-3 S1 profiles,
  with and without organic supports at 35°. Three are 0.01 g off, and Benchy
  on the S1 with organic supports is 0.15 g off (0.7%).
- The 40 mm sphere and 3DBenchy used to crash inside Arachne. That was a
  32-bit `size_t` bug in Orca's `SkeletalTrapezoidation::interpolate`, fixed
  in the patch (REPORT.md §8).
- Speed (Oct 20, 4-core box, Node): linking with mimalloc instead of the
  default allocator cut the 28-case matrix from 240 s to 138 s with the same
  grams. Organic supports went from 10-28 s to 4.6-12.9 s. Emscripten's
  default allocator takes one global lock and Orca's threads allocate all
  the time.
- Warm slices are cheap: `kz_slice()` keeps the default config and the
  flattened profiles between calls, and a second slice of the 20 mm cube
  takes about 150 ms. The remaining 2 s floor is starting the runtime and
  its four threads, which the page pays once.
- Pricing build (Oct 20): 6.5 MB raw, 2.0 MB gzip, 1.42 MB brotli 11. It
  stops after `Print::process()` and sums the plastic in the toolpaths with
  the flow ratios `GCode::_extrude` applies, instead of writing G-code. On
  16 cases (8 models, supports on/off) that's within 0.007 g of Orca's own
  G-code totals, and slices are 27-70% faster (Benchy 6.2 s to 4.0 s in
  Node). File readers, presets, the G-code writer and parser, the prime
  tower and the template parser are replaced by stand-ins in
  `wasm/driver/` so the linker leaves them out.
- `hollowbox40.stl` loads now (Oct 21). admesh decides binary vs ASCII by
  looking for a byte above 127 in the first 128 bytes after the header; a
  binary STL with zero normals and round coordinates has none, so it was
  parsed as ASCII and failed. The pricer writes zero normals, so any such
  model hit it. Native Orca 2.4.2 fails on the same file. The patch now
  also calls a file binary when the facet count in its header gives its
  exact size. Same grams as native (fed an ASCII copy) on all 4 cases, and
  the other 28 cases are unchanged to 0.001 g.
- Clipper fix (Oct 24). Profiling Benchy with supports showed half the
  time on one thread, inside two Clipper offsets in Orca's bridge-over-infill
  code: 0.65 s for one shrink of a 20k point curve on layer 2. At each concave
  corner Clipper adds a point back at the vertex, which makes a spike as long
  as the offset. A curve sampled every 2 µm zigzags at the nanometre level, so
  it gets thousands of them, and the union that cleans them up goes
  quadratic. Native g++ is just as slow (0.48 s), so it's Orca's Clipper, not
  wasm. The patch leaves that point out at almost flat corners (cos ≥
  0.9999 since Oct 25, 0.99999 before), the way Clipper2 does, but only on paths with 1024 or more of
  them. Without that limit organic supports shifted by up to 0.18 g, since
  they react to rounding-level changes. With it all 28 cases and the
  pricer's 8 give the same grams as before. Pricer settings, Node, 4 cores:
  Benchy 3.7 s to 2.3 s, Benchy with supports 5.0 s to 3.8 s, the 7-case
  bench 14.1 s to 11.0 s, the matrix 35.2 s to 28.8 s.
- Threads (Oct 24): up to 8 now (was 4), from `navigator.hardwareConcurrency`,
  in both the TBB cap in the driver and `PTHREAD_POOL_SIZE`. This box has 4
  cores, so the gain isn't measured: 2, 3 and 4 threads fit about 5.8 s of
  serial work plus 19.6 s that splits across threads, which predicts about
  23% less time on 8 cores. Grams are the same at every thread count. 8
  workers start in about 25 ms more than 4 in Chromium, and the heap stays at
  its initial 256 MB on every test model either way. Running 8 threads on
  4 cores is about 20% slower than 4, but `hardwareConcurrency` reports the
  real logical core count, so that only happens if a browser lies about it.
  Still to check on a real phone.
- Second round (Oct 25), from a profile of Benchy, cup, invcone, sphere40
  and mushroom on both printers. Benchy on the P2S ran 7.3 s of CPU in 3.1 s
  wall on 4 cores; 42% of the wall had one thread or none busy. Four changes,
  all with the same grams on the 28 cases, the 18 pricer cases (9 models,
  supports on and off) and at 2, 3, 4 and 8 threads:
  - Organic supports are generated while infill and ironing run, once
    `prepare_infill()` is done (they read its bridge surfaces, so not
    sooner). Other support styles read the infill and stay in order.
  - The convex hull (qhull) is built on first use instead of on load. The
    pricing build never needs it: the lowest point comes from the mesh
    vertices, which gives the same number. Benchy's load went from about
    0.5 s to 0.2 s, and bigger meshes save more.
  - Two steps only the G-code writer uses are skipped (curled-extrusion
    estimate, overhangs for z hop). The wasm is 360 KB smaller (6.15 MB).
  - The Clipper flat-corner limit went from cos 0.99999 to 0.9999. One
    10k-point offset on Benchy's second layer went from 203 ms to 6 ms.
  Node, pricer settings, 4 cores, 3 runs each: Benchy 2.35 s to 1.86 s,
  Benchy with supports 3.72 s to 2.97 s, the 7-case bench 11.4 s to 9.9 s,
  the matrix 28.8 s to 24.4 s.
- Third round (Oct 25), from a one-thread profile on the shop's P2S profile
  at 2 walls / 15%. Clipper was 73% of Benchy's time. 12% was one function,
  GetLowermostRec: each time two output fragments merge it walks the whole
  merged ring to find its lowest point, so a ring built from many fragments
  (offsets of finely sampled curves) is walked once per merge. The patch
  keeps each ring's lowest point up to date while the sweep adds points and
  only walks the ring when several points tie for lowest, or after the
  sweep. Same output on 4000 random Clipper jobs (union, intersection,
  difference, xor, every fill rule, trees, offsets, open paths) built
  natively old against new, and the same grams on the 28 cases and the 18
  pricer cases at 2/15 and 3/40. One thread: Benchy 5.3 s to 4.4 s, Benchy
  with supports 9.8 s to 9.1 s. Four threads: the matrix 26.6 s to 24.2 s,
  the 18 pricer cases at 2/15 15.3 s to 13.4 s. What's left in Clipper is
  spread over the sweep itself (building the intersection list, the
  active edge list), with no single step to cut.
- Memory (Oct 26). The wasm memory starts at 256 MB, but a slice only
  touches part of it; what's touched stays resident, because wasm memory
  can't be handed back short of ending the worker. Benchy with supports at
  2/15 touched 82 MB, and 43 MB of that was loading the STL: admesh matches
  triangle edges through a hash table whose pool keeps every edge it ever
  stored. The patch makes the same pairs by sorting the edges by a hash of
  their key and pairing first-in-first-out within each key, in a 5 MB
  array (the table is kept for meshes over 16M edges). Checked by a build
  that runs both on every load and compares neighbours and stats: the same
  on the 10 eval models and 12 random non-manifold meshes, and the same
  grams on the 28 cases. Loading Benchy now touches 28 MB instead of 43,
  and after a few slices the worker holds about 95 MB instead of about
  119. Time is unchanged. The rest is the slice itself (walls about 18 MB,
  supports about 15 MB) and each thread's allocator keeping pages the others
  can't reuse, which is why the second slice settles 20-30 MB higher than
  the first. mimalloc's retain and purge options made no difference.
- Found, not done yet: Voronoi in Arachne falls back to exact big-integer
  maths on round parts (cup on the S1 spends 77% of its time in Arachne).
  The website's pricer uses the classic wall generator, so it doesn't hit
  this.
- Tried on the website and removed (2026-10-25): the first slice after load
  runs 0.2-1.1 s slower while the browser optimises the module. A warm-up
  slice as soon as the module loaded made Benchy's first quote about 0.4 s
  faster, but it made the page lag while the model was loading.
- Tried and dropped (2026-10-25): filling the infill layers most expensive
  first (fill area weighted by density, a shared counter so the order holds
  across threads). Same grams, no measurable change on the shop's profile:
  10 cases, 3 runs, 12.0-12.8 s against 12.2-13.0 s.
- Tried and dropped: -O3 on Clipper and on Voronoi (no change). -O3 with -msimd128. Heavy jobs were 3-7% faster but
  small ones about 1 s slower (bigger module for each thread to load), the
  matrix went from 138 s to 146 s, and the download grew by 320 KB gzip.

## What's next

1. It's the dev pricer's slicer (Karazas-website 72fadf0). Same grams as
   Node in headless Chromium; Benchy takes 6.1 s, smaller models 0.2 to
   1.6 s. Still to do: time startup on a phone.
2. More printers. Profiles are flattened from Orca's own `resources/profiles`
   with `eval/flatten.py`, so any Orca, Prusa or Bambu printer Orca ships can
   be added the same way.
3. Later: a pricer module in the staff panel to import Orca / Prusa / Bambu
   printer and filament profiles, flatten them on the server and publish them
   to the pricer.

When to download the wasm (on page open or on first model drop) is still
open: preloading makes the first quote faster but costs 2 MB on limited data.

## Layout

- `patches/orca-2.4.2-wasm.patch`: the small source patches (STEP and
  JPEG cut out, two 32-bit `size_t` fixes, a missing include, the platform
  check, `KZ_NO_GCODE` guards on G-code export in Print.cpp, admesh's
  binary STL detection and edge matching, the Clipper offset fix, organic supports alongside
  infill, the lazy convex hull and the skipped G-code-only steps). The admesh fix is in
  `deps.sh`'s objects (`libdeps.a`), not libslic3r. Clipper is compiled into
  both: `deps.sh` builds `deps_src/clipper` into `libdeps.a`, and libslic3r's
  `clipper.cpp` includes it again under the `Slic3r` namespace, so rebuild
  both after changing it.
  Apply to a clone of OrcaSlicer at tag v2.4.2.
- `wasm/driver/`: the entry point (`kz_slice()`: STL + flattened profiles +
  `key value` overrides in, grams out; `-DKZ_GCODE` also writes G-code) and
  the link-time stand-ins. `wasm/roots.sh` compiles them, `wasm/link_web.sh`
  links the pricer's build, `wasm/link_check.sh` the validation build.
- `wasm/cc.sh`, `wasm/deps.sh`, `wasm/link*.sh`: the spike's build scripts.
  `wasm/libslic3r-sources.txt` lists the 208 libslic3r files `cc.sh` was run on.
- `wasm/reach.py`: works out which object files a link actually needs.
- `eval/`: the native reference. `wasm-batch.sh` runs the same 28 cases
  through the wasm build. `run.py` slices one model with the Orca CLI
  and prints grams, `batch.sh` runs the whole matrix, `flatten.py` turns an
  Orca system preset into one flat JSON. `profiles/` has the flattened P2S and
  Ender-3 S1 machine, process and PLA files. 3DBenchy isn't included (11 MB);
  grab it from 3dbenchy.com into `eval/models/benchy.stl`.
- `eval/results/`: native Orca numbers, the wasm parity run, and the compile
  census.

## Building (spike scripts)

The scripts expect everything under `$ORCA_WASM` (default `~/orca-wasm`) and
write objects and output to `$OUT` (default `$ORCA_WASM/build`):

- `orca/`: OrcaSlicer v2.4.2 with `patches/orca-2.4.2-wasm.patch` applied
- `emsdk/`: Emscripten 6.0.13
- `deps-src/`: Boost 1.84 (built to `bwasm/stage/lib`), oneTBB (`build-mt2`,
  built with `-fwasm-exceptions`), qhull 8.0.2 (`bwasm2`, built with
  `-sSUPPORT_LONGJMP=wasm`), Eigen 5.0.1, cereal, CGAL 5.6.3
- `prefix-mt/include`: merged headers for the above

The dependency builds were done by hand and aren't scripted yet; REPORT.md §1
has the flags that matter. Turning that into one reproducible build is step 2
of the plan in §10.

Then: run `cc.sh` on every file in `libslic3r-sources.txt`, run `deps.sh`,
pack the objects into `libslic3r_all.a` and `libdeps.a` with `emar`, compile
the two driver files into `roots/`, and run `link_fixed.sh`. To test:

```
node $OUT/out/orca_fixed.js model.stl "machine.json;process.json;pla.json" out.gcode \
  enable_support 1 support_type "tree(auto)" support_style organic support_threshold_angle 35
```

It prints `rc=0 time_s=... filament_mm=... filament_g=...`.

## Getting a build onto the website

`wasm/publish.sh` copies `out/web/kz-slicer.{js,wasm}` into `dist/` with a
`dist/BUILD` note (source commit, patch hash). Commit and push `dist/` to main.
`.github/workflows/website-import.yml` then puts it on the website's dev
branch (needs the `WEBSITE_TOKEN` secret); without the token, the website's
own `slicer import` workflow picks it up on its next dev push. Either way the
website's `build/slicer-import.sh` minifies the js, slices four models with
the old and new build, and commits with both sets of grams in the message.
