# Karaza slicer

OrcaSlicer's slicing core (libslic3r, v2.4.2) compiled to WebAssembly, so the
3D pricer can slice a customer's model in their browser with the same engine
and the same profiles the shop prints with. It replaces Kiri:Moto, which the
pricer uses today.

This branch has no history in common with the website. It's here because the
session that started it couldn't create a separate repo. Move it to its own
repo whenever that's possible; nothing in it depends on the website.

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
- Tried and dropped: -O3 with -msimd128. Heavy jobs were 3-7% faster but
  small ones about 1 s slower (bigger module for each thread to load), the
  matrix went from 138 s to 146 s, and the download grew by 320 KB gzip.

## What's next

1. Put it in the pricer: a Web Worker that loads the module once (on page
   open or first model drop) and calls `kz_slice()`, the COOP/COEP headers
   threads need, and a check in a real browser. Kiri stays as the fallback
   until this is on dev and timed.
2. More printers. Profiles are flattened from Orca's own `resources/profiles`
   with `eval/flatten.py`, so any Orca, Prusa or Bambu printer Orca ships can
   be added the same way.
3. Later: a pricer module in the staff panel to import Orca / Prusa / Bambu
   printer and filament profiles, flatten them on the server and publish them
   to the pricer.

When to download the wasm (on page open or on first model drop) is still
open: preloading makes the first quote faster but costs 2 MB on limited data.

## Layout

- `patches/orca-2.4.2-wasm.patch`: the seven small source patches (STEP and
  JPEG cut out, two 32-bit `size_t` fixes, a missing include, the platform check).
  Apply to a clone of OrcaSlicer at tag v2.4.2.
- `wasm/driver/`: the entry point (`orca_slice()`: STL + flattened profiles +
  `key value` overrides in, G-code and grams out) and link-time shims.
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
