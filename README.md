# Karaza slicer

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
- Tried and dropped: -O3 with -msimd128. Heavy jobs were 3-7% faster but
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
  check, `KZ_NO_GCODE` guards on G-code export in Print.cpp, and admesh's
  binary STL detection). The admesh fix is in `deps.sh`'s objects
  (`libdeps.a`), not libslic3r.
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
