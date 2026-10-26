# OrcaSlicer PR 15969 (Clipper2 2.0.1) in the wasm build of Orca 2.4.2

Result: the port builds, links and runs the full 54-slice gate. Total slicing time falls by about **1.27x**
(22% less time); plastic weight changes by a median 0.0002 g (0.001 %), at most 0.034 g (0.39 %), and only in a handful
of support cases. Nothing was committed or pushed, and nothing outside `c2/` and `c2build/` was edited (the
read-only tree `/home/user/orca-wasm/orca` and `/home/user/kz-rewrite` were only read).

## 1. What was ported

Source tree: `c2/orca/` (a copy of src/, deps_src/, tests/), diff in `c2/clipper2.patch` (72 files, src/ and deps_src/ only,
12 k lines). The diff base is `git HEAD + patches/orca-2.4.2-wasm.patch`, kept as `c2/pure/`; see section 5 for why that base and not the live tree.

* The PR diff (`c2/pr/pr.diff`, 78 files) applied with `patch -p1`. Most hunks applied with small offsets; the whole of Clipper2 2.0.1 with Orca's tweaks
  (engine, offset, rectclip, new triangulation, clipper2_z, core/export headers) came in unchanged. That includes everything you listed:
  offset joins at the edge-offset crossing for concave corners, the single PolyTree pass for booleans to ExPolygons,
  the Clipper1 semantics (collinear removal, miter limit >= 2, arc tolerance <= quarter offset, 0.005*|delta| input decimation),
  ClipperZUtils on the Clipper2 Z build, and the arrange infinite-bed clamp to +-2^50.
  The version header now says 2.0.1 (was 1.5.2).
* Removed (as in the PR): `deps_src/clipper/` (Clipper1, including our own wasm patch to it, the "KzBot" bottom-point cache), `src/libslic3r/clipper.{cpp,hpp}`,
  `Clipper2Utils.{cpp,hpp}`, `Clipper2ZUtils.hpp`. Added `ClipperZUtils.cpp`.
* Hand work, because 2.4.2 differs from the PR's base:
  * `ClipperUtils.cpp`: the big hunk (the 500-line Clipper1 -> Clipper2 rewrite) did not apply because 2.4.2 has extra functions the PR base lacks
    (`diff_clipped` for ExPolygons, `clip_clipper_polygons_with_subject_bbox(ExPolygons)`, `intersection(.., PolyFillType)`).
    I did a 3-way `git merge-file` of that region (base = PR pre-image, ours = 2.4.2, theirs = PR post-image). One conflict, resolved to the PR side. Then the include hunk and the `_clipper_ln(ClipType ...)` signature by hand.
  * `PrintObject.cpp`: the PR's hunks did not apply (2.4.2 has no `offset_ex_2` calls there); I only renamed `ClipperLib::jtXxx` to `jtXxx` (5 places). Same rename in `Print.cpp` (1 hunk that rejected).
  * `Algorithm/LineSplit.hpp` (`ClipperLib_Z::cInt` -> `coord_t`) and `ShortestPath.hpp` (removed the `ClipperLib::PolyNode` forward declaration): rejected hunks redone by hand.
  * `GCode.cpp` (not in the PR): one `ClipperLib::PointInPolygon(..) != 1` in the wipe tower travel code replaced by `!Slic3r::contains(bed_polygon, p, false)`. This has the same semantics because Clipper1 returned -1 on the boundary. (GCode.cpp is not on the pricing path.)
* Left out: `PerimeterGenerator.cpp` 5 hunks (they touch `clip_inner_walls_over_top`, which 2.4.2 doesn't have); test hunks that rejected
  (`test_fill.cpp`, `test_gcodewriter.cpp`: tests are not built here); `docs/HLSD/polygon-clipping.md` is in `c2/orca/docs` but not in the patch.
  The rejected hunks are in `c2/rej/`.

### tbbmalloc
The PR's Clipper2 engine allocates its nodes (Vertex, OutPt, OutRec, Active, LocalMinima, PolyPath) through `scalable_malloc/scalable_free` via class `operator new/delete`.
**No change was needed**: the repository's `kz/stubs/link_shims.cpp` already defines the `scalable_*` symbols as wrappers around `malloc/free`, and `-sMALLOC=mimalloc` makes that the thread-caching allocator.
So the PR's pooled-node allocation is mimalloc here. I did not write a pool. (`<oneapi/tbb/scalable_allocator.h>` needs `-isystem prefix-mt/include` when compiling the Clipper2 sources; `c2/c2deps.sh` does that.)
CMake still links `TBB::tbbmalloc`, which our scripts don't use.

## 2. How it was built

All of it with the repo's own scripts where possible (`ORCA_WASM=c2`, `OUT=c2build`), at most 2 compiles at a time:

1. `c2/c2deps.sh`: compiled the 5 Clipper2 sources (engine, offset, rectclip, triangulation, clipper2_z) in the way `deps.sh` does. New `libdeps.a` = baseline `libdeps.a` minus the Clipper1 objects (`clipper_clipper*.o`) and the old `c2_*.o`, plus the new ones.
2. Which libslic3r objects to rebuild: `c2/deps_scan.sh` runs `em++ -MM` on all 208 sources and I compared the header sets with the changed files: **160 of 197 archived objects** include a changed header (nearly everything, through ClipperUtils.hpp/Polygon.hpp). `c2/build_affected.sh` rebuilt those 160 plus the new `ClipperZUtils.cpp` with `wasm/cc.sh` (~12 s per file, about 12 min wall). The rest of `libslic3r_all.a` is taken from the existing archive; `clipper.cpp.o` and `Clipper2Utils.cpp.o` were deleted from it.
   `MeshBoolean.cpp` fails with the CGAL "You need LEDA or CORE" error: that already happens in the baseline (it is not in the baseline archive either).
3. `c2/roots2.sh` (roots.sh with 2 jobs) rebuilt the kz roots (entry, pipeline, pipeline_check, print, volume_sum, stubs); `c2/link_c2.sh` is `link_web.sh` with the sources.txt path made absolute. DEST=cfg gives `c2build/out/cfg/kz-slicer.{js,wasm}` (6.07 MB wasm, baseline 6.16 MB).

Build problems: only two, both in `ClipperUtils.cpp` (a lost closing brace of `namespace ClipperUtils {` in the hand merge giving "ambiguous call" errors; the signature of `_clipper_ln`). Everything else compiled the first time.
Files were compiled without `-Werror`; the PR's Clipper2 CMake wants `-Wall -Wextra -Werror`, not checked here.

## 3. Measurements

Script `c2/gate2.cjs` (the gate.cjs matrix, 54 slices, per-case grams and ms, Benchy from `/home/user/karaza-slicer/eval/models/benchy.stl`), driver `c2/runall.sh`. Three builds, run interleaved cfg, c2, pure, three rounds, one node process per run (raw JSON in `c2/runs/`, table in `c2/table.md`, `c2/table.cjs` makes it):

* `cfg`: the baseline you gave (`kzcfg/out/cfg/kz-slicer.js`).
* `pure`: the archive and roots from `kzrw` linked by me (`c2build/pure`). It differs from cfg by 9 objects (FillBase, GCodeProcessor, Layer, LayerRegion, Print, PrintObject, PrintObjectSlice, TreeSupport, TreeSupport3D, a config experiment by someone else). **Grams are identical between cfg and pure in all 54 cases** and timing is within noise, so either is a valid baseline; the table compares against `pure`, which is the same code as c2 minus the PR.
* `c2`: this port.

The machine was loaded by the other build the whole time (load average 4.5 to 7 on 4 cores), so absolute times are noisy; the interleaving is for that.

Total time of the 54 slices per round (ms):

| round | cfg | pure | Clipper2 | Clipper2 vs cfg | Clipper2 vs pure |
|---|---|---|---|---|---|
| 1 | 80810 | 81895 | 63398 | 1.27x | 1.29x |
| 2 | 79048 | 66749 (noisy) | 54213 | 1.46x | 1.23x |
| 3 | 78531 | 80461 | 62487 | 1.26x | 1.29x |
| sum of per-case medians | 79612 | 79145 | 62269 | **1.28x** | **1.27x** |

Where the time goes: cup 1.7 to 2.2x, sphere40 without supports 1.6x, Benchy 1.2 to 1.3x, organic-support cases about 1.2x (tree support spends most of its time elsewhere), very small cases (cube20, hollowbox40 no support) are 50 to 150 ms and within noise (0.87 to 1.3x).

Gram differences, Clipper2 minus baseline (identical for cfg and pure), 54 cases:
* largest 0.0345 g (invcone w4 i80 with supports, -0.24 %); the largest relative is -0.39 % (invcone w2 i15 s1, 0.034 g)
* median 0.00018 g (0.0011 %), 75th percentile 0.0069 g, 90th percentile 0.019 g
* 16 cases within 1e-5 g, 34 within 0.001 g, **20 of 54 are above the 0.001 g limit** of the gate; 15 of those 20 are with supports. Without supports the largest is 0.0035 g (Benchy w3 i40) and the other no-support ones above 0.001 g are sphere-coarse (0.0012 to 0.0018) and sphere40 w2 (-0.0019).
* signed sum over the 54 cases: -0.068 g, so no systematic bias. Benchy and mushroom get slightly heavier with supports (+0.02, +0.007 g), invcone, hollowbox40 and sphere40 slightly lighter.
* So `gate.cjs --tol=1` would pass; with the owner's 0.001 g limit the port would fail the gate, because the results are no longer bit-identical (toolpaths from the offsets differ in the last scaled micron or two and the organic supports amplify that).

Peak memory: `HEAP8` is not exported by the build, so I recorded the node process peak RSS (`maxRSS`), which is dominated by the Benchy mesh: baseline 455 to 484 MB, pure 485 to 592 MB, Clipper2 480 to 485 MB. No visible change.

Organic supports (optional check): validation builds (`link_check.sh` equivalent, `c2build/out/check`, `c2build/pure/out/check`), G-code of invcone and mushroom with supports (w3 i40) drawn with `eval/supports_view.py` (`c2/sup/*.png`). Invcone: support length 33786 mm baseline vs 33415 mm (-1.1 %), same 48 support layers, pictures look the same. Mushroom: 73887 vs 73966 mm (+0.11 %). Nothing looks broken.

## 4. Per-case table

Grams from filament mm (same factor as gate.cjs); ms are medians of the three interleaved runs. "diff" is Clipper2 minus pure (= cfg).

| case | cfg g | pure g | Clipper2 g | diff g (vs pure) | diff % | cfg ms | pure ms | Clipper2 ms | speedup (vs pure) |
|---|---|---|---|---|---|---|---|---|---|
| benchy w3 i40 s0 | 14.6674 | 14.6674 | 14.6709 | +0.0035 | +0.024 | 3774 | 3876 | 3052 | 1.27x |
| benchy w3 i40 s1 | 19.2820 | 19.2820 | 19.3050 | +0.0229 | +0.119 | 6217 | 6260 | 5033 | 1.24x |
| benchy w2 i15 s0 | 12.1240 | 12.1240 | 12.1245 | +0.0004 | +0.003 | 3198 | 2969 | 2208 | 1.34x |
| benchy w2 i15 s1 | 16.7387 | 16.7387 | 16.7586 | +0.0199 | +0.119 | 6652 | 6162 | 5209 | 1.18x |
| benchy w4 i80 s0 | 17.3692 | 17.3692 | 17.3690 | -0.0002 | -0.001 | 2716 | 2493 | 1889 | 1.32x |
| benchy w4 i80 s1 | 21.9839 | 21.9839 | 22.0031 | +0.0192 | +0.087 | 6177 | 5845 | 4609 | 1.27x |
| bracket w3 i40 s0 | 9.1251 | 9.1251 | 9.1251 | +0.0000 | +0.000 | 111 | 128 | 95 | 1.35x |
| bracket w3 i40 s1 | 9.1251 | 9.1251 | 9.1251 | +0.0000 | +0.000 | 121 | 123 | 97 | 1.27x |
| bracket w2 i15 s0 | 6.8992 | 6.8992 | 6.8992 | +0.0000 | +0.000 | 80 | 78 | 59 | 1.32x |
| bracket w2 i15 s1 | 6.8992 | 6.8992 | 6.8992 | +0.0000 | +0.000 | 93 | 84 | 74 | 1.14x |
| bracket w4 i80 s0 | 11.2274 | 11.2274 | 11.2274 | +0.0000 | +0.000 | 203 | 204 | 182 | 1.12x |
| bracket w4 i80 s1 | 11.2274 | 11.2274 | 11.2274 | +0.0000 | +0.000 | 234 | 223 | 180 | 1.24x |
| cube20 w3 i40 s0 | 5.8081 | 5.8081 | 5.8081 | +0.0000 | +0.000 | 84 | 85 | 70 | 1.21x |
| cube20 w3 i40 s1 | 5.8081 | 5.8081 | 5.8081 | +0.0000 | +0.000 | 91 | 90 | 67 | 1.34x |
| cube20 w2 i15 s0 | 3.7442 | 3.7442 | 3.7442 | +0.0000 | +0.000 | 60 | 52 | 44 | 1.18x |
| cube20 w2 i15 s1 | 3.7442 | 3.7442 | 3.7442 | +0.0000 | +0.000 | 61 | 44 | 49 | 0.90x |
| cube20 w4 i80 s0 | 8.2713 | 8.2713 | 8.2713 | +0.0000 | +0.000 | 143 | 158 | 157 | 1.01x |
| cube20 w4 i80 s1 | 8.2713 | 8.2713 | 8.2713 | +0.0000 | +0.000 | 136 | 152 | 175 | 0.87x |
| cup w3 i40 s0 | 16.7987 | 16.7987 | 16.7986 | -0.0000 | -0.000 | 770 | 771 | 375 | 2.06x |
| cup w3 i40 s1 | 16.7987 | 16.7987 | 16.7986 | -0.0000 | -0.000 | 886 | 935 | 453 | 2.06x |
| cup w2 i15 s0 | 16.8100 | 16.8100 | 16.8100 | -0.0000 | -0.000 | 683 | 650 | 348 | 1.87x |
| cup w2 i15 s1 | 16.8100 | 16.8100 | 16.8100 | -0.0000 | -0.000 | 957 | 886 | 401 | 2.21x |
| cup w4 i80 s0 | 16.8069 | 16.8069 | 16.8068 | -0.0000 | -0.000 | 732 | 757 | 338 | 2.24x |
| cup w4 i80 s1 | 16.8069 | 16.8069 | 16.8068 | -0.0000 | -0.000 | 904 | 808 | 476 | 1.70x |
| hollowbox40 w3 i40 s0 | 20.7104 | 20.7104 | 20.7103 | -0.0001 | -0.000 | 94 | 74 | 75 | 0.99x |
| hollowbox40 w3 i40 s1 | 26.7151 | 26.7151 | 26.7054 | -0.0097 | -0.036 | 1690 | 1806 | 1473 | 1.23x |
| hollowbox40 w2 i15 s0 | 20.6992 | 20.6992 | 20.6993 | +0.0000 | +0.000 | 89 | 80 | 62 | 1.29x |
| hollowbox40 w2 i15 s1 | 26.7040 | 26.7040 | 26.6944 | -0.0097 | -0.036 | 1775 | 1849 | 1442 | 1.28x |
| hollowbox40 w4 i80 s0 | 20.7055 | 20.7055 | 20.7055 | +0.0000 | +0.000 | 102 | 97 | 65 | 1.49x |
| hollowbox40 w4 i80 s1 | 26.7103 | 26.7103 | 26.7006 | -0.0097 | -0.036 | 1724 | 1823 | 1532 | 1.19x |
| invcone w3 i40 s0 | 8.0514 | 8.0514 | 8.0514 | -0.0000 | -0.000 | 488 | 447 | 331 | 1.35x |
| invcone w3 i40 s1 | 11.2287 | 11.2287 | 11.1943 | -0.0344 | -0.306 | 4850 | 5126 | 4336 | 1.18x |
| invcone w2 i15 s0 | 5.5132 | 5.5132 | 5.5136 | +0.0004 | +0.007 | 465 | 482 | 312 | 1.54x |
| invcone w2 i15 s1 | 8.6905 | 8.6905 | 8.6565 | -0.0340 | -0.391 | 4749 | 4796 | 4044 | 1.19x |
| invcone w4 i80 s0 | 11.3163 | 11.3163 | 11.3162 | -0.0001 | -0.001 | 577 | 591 | 542 | 1.09x |
| invcone w4 i80 s1 | 14.4937 | 14.4937 | 14.4592 | -0.0345 | -0.238 | 4972 | 5180 | 4254 | 1.22x |
| mushroom w3 i40 s0 | 9.2994 | 9.2994 | 9.2994 | +0.0000 | +0.000 | 141 | 137 | 111 | 1.23x |
| mushroom w3 i40 s1 | 16.1913 | 16.1913 | 16.1982 | +0.0069 | +0.042 | 2030 | 1883 | 1391 | 1.35x |
| mushroom w2 i15 s0 | 7.0379 | 7.0379 | 7.0379 | +0.0000 | +0.000 | 70 | 48 | 53 | 0.91x |
| mushroom w2 i15 s1 | 13.9299 | 13.9299 | 13.9367 | +0.0068 | +0.049 | 1897 | 1745 | 1605 | 1.09x |
| mushroom w4 i80 s0 | 11.9365 | 11.9365 | 11.9365 | +0.0000 | +0.000 | 192 | 194 | 176 | 1.10x |
| mushroom w4 i80 s1 | 18.8284 | 18.8284 | 18.8353 | +0.0069 | +0.036 | 2120 | 1769 | 1656 | 1.07x |
| sphere-coarse w3 i40 s0 | 20.2367 | 20.2367 | 20.2381 | +0.0014 | +0.007 | 519 | 539 | 422 | 1.28x |
| sphere-coarse w3 i40 s1 | 20.5700 | 20.5700 | 20.5698 | -0.0002 | -0.001 | 917 | 1078 | 847 | 1.27x |
| sphere-coarse w2 i15 s0 | 11.1465 | 11.1465 | 11.1483 | +0.0018 | +0.016 | 308 | 396 | 318 | 1.25x |
| sphere-coarse w2 i15 s1 | 11.4799 | 11.4799 | 11.4801 | +0.0002 | +0.002 | 899 | 885 | 719 | 1.23x |
| sphere-coarse w4 i80 s0 | 31.9705 | 31.9705 | 31.9717 | +0.0012 | +0.004 | 730 | 835 | 661 | 1.26x |
| sphere-coarse w4 i80 s1 | 32.3038 | 32.3038 | 32.3035 | -0.0004 | -0.001 | 1294 | 1337 | 1024 | 1.31x |
| sphere40 w3 i40 s0 | 20.7716 | 20.7716 | 20.7714 | -0.0002 | -0.001 | 1306 | 1389 | 862 | 1.61x |
| sphere40 w3 i40 s1 | 21.4347 | 21.4347 | 21.4277 | -0.0070 | -0.033 | 2521 | 2545 | 1967 | 1.29x |
| sphere40 w2 i15 s0 | 11.4484 | 11.4484 | 11.4465 | -0.0019 | -0.017 | 1178 | 1236 | 769 | 1.61x |
| sphere40 w2 i15 s1 | 12.1115 | 12.1115 | 12.1028 | -0.0087 | -0.072 | 2361 | 2444 | 1948 | 1.25x |
| sphere40 w4 i80 s0 | 32.8293 | 32.8293 | 32.8286 | -0.0007 | -0.002 | 1789 | 1649 | 1397 | 1.18x |
| sphere40 w4 i80 s1 | 33.4924 | 33.4924 | 33.4849 | -0.0075 | -0.022 | 2682 | 2852 | 2235 | 1.28x |

## 5. Notes and caveats

* **The live source tree changed under me.** `/home/user/orca-wasm/orca` is no longer the patch alone: while I worked, someone else's experiment left uncommitted edits in `Fill/FillBase.cpp`, `GCode/GCodeProcessor.cpp`, `Print.cpp` (+9 lines), `PrintObject.cpp`, `PrintObjectSlice.cpp`, `Support/TreeSupport.{cpp,hpp}` on top of `patches/orca-2.4.2-wasm.patch` (`git diff` there differs from the patch file). My copy (made earlier) matched the patch, and I double-checked by rebuilding the base from `git archive HEAD` plus the patch (`c2/pure`). `clipper2.patch` is `diff -ruN c2/pure/{src,deps_src} c2/orca/{src,deps_src}`, so it applies to HEAD + the official patch, not to the live tree as it is now. If you apply it there, FillBase.cpp and Print.cpp (PR hunks) may need a manual fix.
* The 160 objects were compiled with the same flags as the baseline (`cc.sh`, -O2). Link flags unchanged.
* Not done: native Orca comparison, the unit tests of the PR, checking that the PR's behaviours (miter limit, decimation, etc.) are exercised.
* Files: `c2/orca` (ported tree), `c2/pure` (base), `c2/pr/pr.diff`, `c2/rej/` (rejected hunks), `c2/runs/*.json`, `c2/table.md`, `c2/sup/`, build dir `c2build/` (archives `libslic3r_all.a`, `libdeps.a`, `out/cfg/kz-slicer.js`).
