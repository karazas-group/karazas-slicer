# Modular rewrite plan

Goal: the pricer's slicer as one directory, `kz/`, with one file per stage or
function, so each piece can be made smaller and faster on its own. The grams
stay exactly what they are today. Over time nothing of Orca is left in the
path. The wasm build, the patch and the profiles stay as they are until a
stage replaces them.

The pricer takes an STL and settings and returns grams of filament. It never
writes G-code. That is the freedom: any Orca code that only exists to make
G-code, or to serve a setting the pricer never sends, can go.

Status: stage 1 is done (section 6). Everything after it is a plan, and
section 7 lists what has not been verified.

## 1. The gate

Every step ends with all 28 cases of the matrix identical to the grams in
`eval/golden-grams.txt` (7 models x P2S/S1 x supports off / organic at 35 deg).
Nothing is committed to `rewrite` otherwise.

```
bash wasm/roots.sh                      # compile kz/sources.txt into $OUT/roots
bash wasm/link_web.sh                   # ~35 s, DEST=<name> for a new output dir
node eval/gate.cjs $OUT/out/<name>/kz-slicer.js     # 28 cases, exit 0 only if identical
```

`gate.cjs` needs `benchy.stl` (not in the repo, 11 MB): `BENCHY=/path/benchy.stl`
or put it in `eval/models/`. Grams are printed rounded to 0.01 g, which is a
weak test: a change of 1e-4 g passes it. Stage 1 therefore also compared the
whole JSON (`filament_mm` to 0.1 mm as well) on the 28 cases between the old
and new build, byte for byte. Each later stage should add a stricter check of
its own output, cheaper than the whole pipeline: dump the stage's output
(polygons, paths, volumes as exact integers or `%a` doubles) from the old and
the new build and compare those files. That also finds a difference that
rounding hides in grams today and a different input shows tomorrow. The
golden list was taken from the build before stage 1; it equals the previous
`matrix-final.txt`.

The matrix covers the P2S profile (classic walls) and the S1 profile, whose
`wall_generator` is unset, so it takes Orca's default, Arachne. So Arachne is
in the gate. It can only go once the owner says the pricer never prices with
it (section 5).

## 2. What runs today

Cited from the patched tree (`orca/src/libslic3r`, Orca 2.4.2 plus
`patches/orca-2.4.2-wasm.patch`).

```
kz_slice (kz/entry.cpp)                   thread limits, override parsing
 kz::slice_one (kz/pipeline.cpp)
  load_stl            Format/STL.cpp:load_stl
                      TriangleMesh.cpp:ReadSTLFile -> admesh stl_open,
                      stl_check_facets_exact (connect.cpp), repair -> from_stl
  Model::add_object, ensure_on_bed, center_instances_around_point
  load_profiles       PrintConfig.cpp:full_print_config + load_from_json + apply
  Print::apply        PrintApply.cpp:Print::apply (+ config diffing, regions)
  Print::validate     Print.cpp:Print::validate
  Print::process      Print.cpp:Print::process
   PrintObject::make_perimeters
    PrintObject::slice                        PrintObjectSlice.cpp:817
      generate_object_layers                  Slicing.cpp
      slice_volumes -> slice_volumes_inner    PrintObjectSlice.cpp:1149
        slice_mesh / slice_mesh_ex            TriangleMeshSlicer.cpp
        (apply_mm_segmentation, fuzzy skin, elephant foot, xy compensation,
         Layer::make_slices (Layer.cpp:38) merge slices per layer)
    Layer::make_perimeters (Layer.cpp:185) -> LayerRegion::make_perimeters
      (LayerRegion.cpp:82) -> PerimeterGenerator (PerimeterGenerator.cpp):
      process_classic, or process_arachne -> Arachne/WallToolPaths ->
      SkeletalTrapezoidation (Voronoi)
   PrintObject::prepare_infill (PrintObject.cpp:560)
      detect_surfaces_type (1520), prepare_fill_surfaces (LayerRegion.cpp),
      discover_vertical_shells (2008), discover_horizontal_shells (3955),
      process_external_surfaces (1926; BridgeDetector.cpp),
      clip_fill_surfaces (3869), bridge_over_infill (2468), combine_infill (4166)
   PrintObject::infill (699) -> Layer::make_fills (Fill/Fill.cpp:1213), per
      layer in parallel_for; Fill::new_from_type -> Fill*.cpp
      (rectilinear/monotonic, crosshatch, grid, gyroid, lines, ...)
   PrintObject::generate_support_material (850) -> _generate_support_material
      (4289) -> organic tree support: generate_tree_support_3D
      (Support/TreeSupport3D.cpp:4181) with TreeModelVolumes.cpp and
      SupportCommon.cpp. Runs beside infill since the patch (Print.cpp
      kz_support_group).
   PrintObject::ironing (728)               no-op unless ironing is on
   Print::_make_skirt (Print.cpp:2679), brim (Brim.cpp)
   GCode::collect_layers_to_print           kz/stubs/gcode_layers.cpp
  kz_extruded_volume                        kz/volume_sum.cpp
```

Not run in the pricing path: the G-code writer, GCodeProcessor, wipe tower
(`kz/stubs/wipe_tower.cpp` stands in), the template parser, 3MF/OBJ/AMF
readers, `estimate_curled_extrusions`, `detect_overhangs_for_lift`.

Clipper is under all of it. `ClipperUtils.cpp` wraps `clipper.cpp`, which is
compiled twice (into `libdeps.a` and included into libslic3r), and the patch
changes it in three places. It stays a library: every stage calls it. It is
the one piece that is not a stage.

## 3. Where the time and memory go today

Node, this box (4 cores, noisy, other jobs ran at the same time, so read
these to +-10%), the build before stage 1 and the one after, P2S profile from
`eval/profiles`, first slice after load (cold: wasm still being optimised).
`LAPS=1 node memb.cjs`. A lap is the time since the previous marker and is
printed with the name of the step that starts next, so "Generating walls" is
the time of `slice()`, "Generating infill regions" the perimeters, "Generating
infill toolpath" `prepare_infill`, and the next lap (support or skirt)
`infill`.

Benchy, supports off (2.9 s):

| Stage | ms | share | wasm RSS after |
|---|---|---|---|
| load STL (admesh) | 450 | 16% | 28 MB (from 2) |
| profiles + apply + validate + setup | 175 | 6% | 32 MB |
| slice (mesh to layer polygons) | 635 | 22% | 59 MB (+27) |
| perimeters (classic) | 590 | 20% | 60 MB |
| prepare_infill | 460 | 16% | 62 MB |
| infill | 450 | 16% | 62 MB |
| skirt, brim, toolpath ordering, sum | 170 | 6% | 62 MB |

Benchy with organic supports (4.7 s): the first four rows are the same size;
`prepare_infill` 480, then infill and the support generator overlap and take
about 2.6 s together (support laps 499 + 1089 + 962 ms), RSS 72 MB.
Sphere40 with supports (2.4 s, 33 MB): load 27 ms, slice 210, perimeters 245,
prepare_infill 254, support about 1.6 s of the rest, and +13 MB in the last
support lap. Cup, supports off (1.2 s, 23 MB): slice 340, perimeters 430,
prepare_infill 90; its RSS jumps +18 MB in the slice.

What that says:

1. For models with supports, the supports are the biggest single piece
   (35-55% of the wall time and the last 10-13 MB). Another agent has it.
2. Without supports, slice, perimeters, prepare_infill and infill are four
   pieces of 16-22% each; load is 16% on a big mesh (Benchy is an 11 MB binary STL, about 225k facets) and
   nothing on small ones (cup 24 ms).
3. Memory: 26 MB is the STL load (already cut once from 43), 27 MB is slicing
   (the sliced layers of every volume, Benchy). Those two are the largest
   non-support memory items, and both are mesh-side, before any config
   machinery matters.
4. Clipper dominates inside slice, perimeters and prepare_infill (README:
   73% of one-thread Benchy time before the last patch). A stage rewrite does
   not remove that cost unless it makes fewer Clipper calls, and fewer calls
   change the output order. That is where exactness is hardest.

## 4. Target layout

One directory. One file per stage; a stage that is several Orca functions
becomes several files only when they are separable by data type. A file owns
its algorithm and its private helpers, includes `kz.hpp` and Orca headers for
the data types (`Polygon`, `ExPolygon`, `Surface`, `ExtrusionEntity`, `Print`)
until those are replaced too, and exports nothing but the functions named in
`kz.hpp`.

```
kz/
  kz.hpp               the interface between stages (types + declarations)
  sources.txt          what is compiled and linked (section 8)
  entry.cpp            DONE  kz_slice(), main(), thread limits, override string
  pipeline.cpp         DONE  slice_one(): order of the stages, profile cache
  volume_sum.cpp       DONE  kz_extruded_volume(): the sum
  stubs/*.cpp          DONE  stand-ins that keep unused Orca code out of the link
  stl_load.cpp         binary/ASCII STL bytes -> indexed_triangle_set, repaired
  mesh_slice.cpp       indexed_triangle_set + layer heights -> polygons per layer
  layers.cpp           layer height profile -> z ranges (generate_object_layers)
  slice_regions.cpp    per-volume slices -> per-layer ExPolygons (Layer::make_slices)
  perimeters_classic.cpp   PerimeterGenerator::process_classic
  perimeters_arachne.cpp   only if Arachne stays (section 5)
  surface_types.cpp    detect_surfaces_type
  shells.cpp           discover_vertical_shells, discover_horizontal_shells
  external_surfaces.cpp    process_external_surfaces + BridgeDetector
  bridge_over_infill.cpp   PrintObject::bridge_over_infill
  fill_*.cpp           one per infill pattern the pricer offers, plus fill_surfaces.cpp
                       (Layer::make_fills orchestration)
  support_organic.cpp  (the other agent's work, planned only here)
  skirt_brim.cpp       Print::_make_skirt, Brim.cpp
  simplify.cpp         PrintObject::simplify_extrusion_path (path resolution pass)
  apply.cpp            Print::apply replacement: config -> per-object settings
  process.cpp          Print::process replacement (the order, the parallelism)
```

Stage contracts (inputs and outputs are Orca types until a stage replaces
the type as well; all coordinates stay `coord_t` = int64 scaled by 1e6, except
the mesh, which is `float`):

| File | In | Out | Replaces |
|---|---|---|---|
| stl_load | bytes of the file | `indexed_triangle_set` (float vertices, int faces), repaired, plus bbox | `load_stl`, `TriangleMesh::ReadSTLFile`/`from_stl`, admesh `stl_open`, `stl_check_facets_exact`, nearby/holes/normals/unconnected passes, `stl_calculate_volume` |
| layers | slicing params, layer height | `vector<coordf_t>` z pairs | `generate_object_layers`, `update_layer_height_profile` (Slicing.cpp) |
| mesh_slice | its, z list | `vector<Polygons>` per layer, closed, orientation fixed | `slice_mesh`, `slice_mesh_ex`, `make_loops`, `chain_lines` in TriangleMeshSlicer.cpp |
| slice_regions | per-volume Polygons per layer, region config | `Layer::lslices`, `LayerRegion::slices` | `slice_volumes_inner`, `Layer::make_slices`, elephant foot, xy compensation |
| perimeters_classic | `LayerRegion::slices`, flow | `LayerRegion::perimeters`, `fill_surfaces`, gap fill | `PerimeterGenerator::process_classic` and its helpers, `LayerRegion::make_perimeters` |
| surface_types .. bridge_over_infill | per-layer slices and perimeters | typed `Surface`s in `fill_surfaces` | the `prepare_infill` list in section 2 |
| fill_* | `fill_surfaces`, density, pattern | `LayerRegion::fills` | `Layer::make_fills`, `Fill*.cpp` |
| skirt_brim | first-layer hull, object slices | skirt/brim `ExtrusionEntityCollection`s | `Print::_make_skirt`, `Brim.cpp` |
| volume_sum | the extrusion entities | mm^3 | done |

Final shape, once the middle stages exist: the extrusion entities need not
exist at all. Stages that now emit `ExtrusionPath`s to be summed can emit a
volume accumulator instead (`mm3_per_mm * length * flow_ratio` per path, in
the same order). That is the largest memory and allocation saving after the
two mesh items, and it is only safe once every consumer of the paths has been
found: infill ordering and `Print::_make_skirt` read earlier paths, and the
support generator reads `fills`. Do it last.

## 5. Config branches the pricer never takes

What the pricer sends: the P2S or S1 flattened machine/process/filament
profile, plus overrides such as `wall_loops`, `sparse_infill_density`,
`enable_support`, `support_type=tree(auto)`, `support_style=organic`,
`support_threshold_angle` and a huge `printable_area`. Everything below is
true only for that set. Dropping a branch means a request that sets it gets
an error or the default, so each needs the owner's agreement and a
fail-loud check in `apply.cpp` (reject a profile whose value is not supported
rather than price it wrong).

Safe to delete on the basis of the code alone (G-code only; nothing in the
slicer reads them):
- G-code writer, `GCodeProcessor`, cooling buffer, pressure advance, arc
  fitting is NOT in this list: `PrintObject::simplify_extrusion_path`
  (Print.cpp:2598, the three "Optimizing toolpath" laps, 5% of Benchy) runs
  before the sum and rewrites wall, infill and support paths at `resolution`
  0.012 mm, so it changes path lengths and therefore grams. It is a stage to
  port (`simplify.cpp`), not to delete. `enable_arc_fitting` is 0 in both
  profiles, so its branch can go if the owner confirms; seam placement is
  G-code export only and is already out), `estimate_curled_extrusions` and
  `detect_overhangs_for_lift` (already skipped).
- Wipe tower and multi-material: `Print::_make_wipe_tower`, `ToolOrdering`,
  `apply_mm_segmentation`, multi-extruder region splitting. The pricer prints
  one material. Note the matrix goes through `ToolOrdering` today because
  `Print::process` and the skirt step use it for `tools_for_layer`; replacing
  that is part of `process.cpp`.
- 3MF/OBJ/AMF/STEP readers, the preset system, the placeholder parser
  (already stubbed).
- Sequential printing (`print_sequence=by object`), raft, spiral vase,
  `contour_z`, conical overhang, fuzzy skin, `slicing_mode` other than
  regular, adaptive layer heights, variable layer height profiles, shared
  objects (`is_print_object_the_same`), multiple objects and instances (the
  pricer prices one model; the sum multiplies by `instances().size()`).

Needs the owner to say the pricer never uses it:
- Arachne (`wall_generator=arachne`). The S1 profile uses it by default and
  the matrix has 14 cases on it. It is also the code with the 77%-of-time
  case in the README and the Voronoi big-integer fallback. Keep it as
  `perimeters_arachne.cpp` unless the owner decides all pricing uses classic.
- Non-organic supports (normal/snug, tree(auto) with `tree_slim`/`tree_strong`/
  `tree_hybrid`). `TreeSupport.cpp` (3786 lines), `SupportMaterial.cpp` (3309)
  and most of `SupportCommon.cpp` serve those. The organic path calls only
  parts of `SupportCommon.cpp` and `TreeSupport3D.cpp`.
- Infill patterns the pricer UI does not offer (`Fill*.cpp`: adaptive cubic,
  lightning, Hilbert, Archimedean, TPMS, 3D honeycomb, ...). Needs the list of
  patterns the website can send. If it is a closed set, the rest of Fill/ goes.
- `ironing_type` other than none, `only_one_wall_*`, `infill_combination`,
  `thick_bridges`, top/bottom pattern variants, `gap_infill` off.

Not deletable without changing grams: anything that edits geometry before
the sum (elephant foot, xy compensation, bridges, gap fill, extra perimeters,
`infill_wall_overlap`, flow ratios). Those stay until their stage is
rewritten and proven equal.

## 6. Stage 1 (done): the split, the build, the gate

What it did: it created `kz/`, moved the driver and the five stand-ins in
(`git mv`, history follows), and split the 259-line driver into three files by
function: `entry.cpp` (exported function, threads, override string),
`pipeline.cpp` (order of the Orca calls, profile cache) and `volume_sum.cpp`
(the extrusion sum). `kz.hpp` is the interface. The build now reads
`kz/sources.txt` (section 8). The logic of every moved function is the old
text without edits, apart from names and the namespace of `lap()`.

Why this and not STL load first: it is the only stage whose result can be
proven identical bit for bit, and it fixes the structure (one file per stage,
sources list, gate) that every later stage uses. It does not make anything
faster: wasm size +1 KB, times unchanged within noise (below). It does not
replace Orca code, because the sum was never Orca code. STL load is stage 2
and replaces Orca for real.

Results:
- Matrix: 28/28 identical to `eval/golden-grams.txt`, and the full
  `kz_slice` JSON (grams and `filament_mm`) byte-identical to the build before
  the change on the same 28 cases. `link_check.sh` (validation build with
  G-code export) links and slices; the cup gives 16.16 g in it too.
- Timing, 3 runs each, `memb.cjs` on benchy, benchy+support, cup,
  sphere40+support, other jobs running on the box: before 8.07 / 8.00 / 8.01 s,
  after 11.8 (a load spike from another job) / 7.58 / 8.13 s. No change.
- Size: 6,155,410 to 6,156,397 bytes.

## 7. Order of work

Smallest and safest first, then by payoff. "Exactness" lists what makes the
stage hard. Each stage is its own commit and passes the gate first.

2. **stl_load.cpp** (payoff: 450 ms and 26 MB on Benchy, nothing on small
   models). Read the bytes, pick binary vs ASCII exactly as the patched admesh
   does (header facet count gives the exact size), build the indexed set,
   repair. Exactness: output must match admesh in vertex order, face order and
   every repair outcome: `stl_check_facets_exact` merges equal vertices by
   float bit pattern and pairs edges first-in-first-out per key (the patch
   already replaced the hash table that way), then `stl_fix_normal_directions`,
   `fill_holes`, `remove_unconnected_facets`, `nearby` (tolerance from
   `shortest_edge`, 2 iterations, TriangleMesh.cpp:102). Anything that is not "already a closed
   manifold" must run the same code. Plan: write it to take the fast path
   only when the mesh is closed manifold after the exact pass (every edge
   shared by 2 facets, consistent orientation) and defer to admesh otherwise
   (keep admesh as the fallback until a fuzzed corpus shows 0 differences).
   Test: dump `its.vertices`/`its.indices` from both and compare `memcmp`
   on the 10 eval models, the 12 random non-manifold meshes the Oct 26
   patch used, and a fuzz set (flipped normals, duplicated facets, holes,
   zero-area facets, NaN). Also compare `TriangleMesh` stats that later code
   reads (`volume`, `bounding_box`, `has_shared_vertices`). Branch to delete:
   the ASCII STL loop, progress callbacks, `custom_header_length` (the
   pricer always writes a binary STL; confirm with the website).
3. **layers.cpp** (small, pure maths, no Clipper): `generate_object_layers`
   with no variable layer height profile (fixed `layer_height` and
   `initial_layer_print_height`). Exactness: the doubles must be the same
   `coordf_t` values; `print_z` and `slice_z` are summed, not multiplied, in
   Orca, so keep the order of additions. Test: dump the pair list for 30
   layer-height inputs.
4. **mesh_slice.cpp** (payoff 22%, and the +27 MB). `slice_mesh` cuts each
   triangle with each layer's plane, chains segments into loops, unions them.
   Exactness: the float-to-int conversion (`scale_` rounds with a specific
   rule), the order of the segments (it comes from the facets sorted by z, a
   TBB `parallel_for` with per-layer output), the loop closing tolerances in
   `chain_lines`, the orientation/winding fix-up and the `ClipperLib::Union`
   call after (Clipper's output point order depends on its input order, so
   the union input must be the same sequence). Test: dump every layer's
   `Polygons` (point lists) from both builds and diff; do it for the whole
   eval set, not only the matrix.
   Memory idea: keep only the layers a later stage still needs (it holds all
   `slice_volumes` results of all volumes at once today).
5. **slice_regions.cpp**: `slice_volumes_inner` and `Layer::make_slices`.
   Drops multi-volume merging, MM segmentation, fuzzy skin, per-region config
   splitting. Exactness: `union_ex` order; `Layer::make_slices` re-sorts
   `lslices` by area, ties break by input order.
6. **perimeters_classic.cpp** (payoff 20%). `process_classic` does repeated
   `offset`/`offset2_ex`/`diff_ex` for each wall, ordering by `inset` then
   `PerimeterGeneratorLoops` nesting, then gap fill and `fill_surfaces` from
   the innermost offset. Exactness: Clipper call order and arguments
   (miter limit, `ClipperLib::jtMiter` vs round), `ExPolygon` hole/contour
   order, `loop` depth sorting with a stable sort, `float` flow numbers
   (`Flow::mm3_per_mm` is `float`; `width` and `spacing` are `float`, the
   mm3 sum is accumulated in `double`). Fewer Clipper calls are tempting and
   will change the grams: change calls only in a later, separate step with
   its own proof. Delete: Arachne selection if allowed, `only_one_wall_*`,
   overhang perimeter detection if `detect_overhang_wall` is off in every
   profile (check; the P2S profile keys must be read first), seam anything.
7. **Prepare-infill files**, one commit each, in the order of the function
   list in section 2: `surface_types`, `shells` (both shell passes, large
   Clipper traffic and layer-neighbour lookups), `external_surfaces` +
   BridgeDetector (bridge angle search by float trial angles, so keep
   `float`), `bridge_over_infill` (the file the Clipper patch is about; its
   cost shows on organic supports because they read its surfaces),
   `combine_infill` (delete if `infill_combination` is never set).
   Each runs per layer in a `parallel_for` over layers with reads of
   neighbouring layers; TBB ordering matters only if a stage writes a layer
   another layer reads in the same pass, so keep the pass boundaries
   (`discover_vertical_shells` reads `layer->lower_layer` slices from the
   previous pass).
8. **fill_*.cpp** (payoff 16%). First `fill_surfaces.cpp`
   (`Layer::make_fills`: group surfaces into `SurfaceFill`s, pick pattern and
   density, then call the fill). Then each pattern as its own file, starting
   with what the pricer uses (rectilinear/monotonic, crosshatch per the
   profiles). Exactness: pattern generation uses `float` for angles and
   line spacing converted with `scale_`; polylines are then
   `chained_path`-ordered by a greedy nearest-neighbour (order only changes
   path order, not the sum, but check that the sum is order independent:
   double addition is not associative, so summing the same paths in another
   order can move the last bit and, rarely, the rounded 0.01 g). `volume_sum`
   already fixes its own order; keep fill output order unless the sum is
   changed to something order independent (integer micro-mm^3 per path would
   do it, and changes the printed grams, so it is a gate-visible step).
9. **skirt_brim.cpp, simplify.cpp**: `Print::_make_skirt` and `Brim.cpp`
   (1019 lines, most is for multi-object and support brim), and
   `simplify_extrusion_path` (about 5% on Benchy). Both small. Simplify
   changes path point sets, so it needs the same polyline dump check as
   fills.
10. **apply.cpp, process.cpp** (payoff: config cost and orchestration, both
    small in time: apply+validate is 6%). Replace `Print::apply` (PrintApply.cpp,
    config diffing and invalidation machinery for a GUI that re-slices) with a
    straight function that builds one `PrintObject` per model. Delete
    `StepStates`, `set_started/set_done`, cancel checks, status callbacks
    (keep `lap` markers), the shared-object logic and the cache path in
    `Print::process` (`use_cache`). This is where the Orca `Print` class is
    finally replaced; only after the stages it calls are in `kz/`.
11. **support_organic.cpp**: another agent. This plan only fixes its
    interface: input is the layers after `prepare_infill` (outlines,
    perimeters, bridge `fill_surfaces`), output is `SupportLayer::support_fills`
    for `volume_sum`. Per the Print.cpp comment it must run beside `infill`.
12. **Last: drop the extrusion entities** (section 4, final shape) and
    **last: drop Clipper**'s Orca wrapper layer for a kz-owned integer
    polygon library, if profiling after all this says it is still the cost.
    That one changes the exactness story completely, since Clipper's output
    is the contract everything above was matched against. It needs its own
    plan.

Payoff order by the table in section 3 would be supports, then
slice/perimeters/load (about equal), then prepare_infill and infill. The order
above is safest first, and puts load and slice first because their output is
easy to dump and compare exactly.

## 8. How the build changes

Today `libslic3r_all.a` holds every Orca object, and a few objects in
`roots/` (built by `roots.sh`) are linked ahead of it so the linker takes
their symbols instead of the archive's. Stage 1 turned that into a list:

`kz/sources.txt`: `name  link  source  [flags]` per line. `roots.sh [name...]`
compiles each line with `cc.sh` (same flags as the Orca files) into
`$OUT/roots/<name>.o`; `link_web.sh` links the `web` and `both` lines;
`link_check.sh` the `check` and `both` lines. `cc.sh` takes an `OBJ` name so
two builds of one source can coexist.

To replace an Orca file with a kz stage:
1. Write `kz/<stage>.cpp` defining the same symbols, add a line to
   `sources.txt`.
2. Link. If the stage replaces the whole Orca object, nothing from that
   archive member is pulled and the Orca code is gone from the wasm. If it
   defines only some of the member's symbols and something else still needs
   the rest, the link fails with `duplicate symbol`. That error is the check
   that the replacement is complete; do not paper over it with
   `--allow-multiple-definition`.
3. Once a stage is proven, also delete the Orca object from the archive
   (`emar d libslic3r_all.a <member>`) and the file from
   `wasm/libslic3r-sources.txt`, so the next fresh build cannot silently use
   the old code. Later, when `kz/` holds most of the path, the archive step
   goes away and `sources.txt` lists the full set.
4. Run `eval/gate.cjs`, then the stage's own dump comparison.

Things that will bite:
- Orca headers are still included by the kz files, so changes to a stage
  that touches a type (`Surface`, `LayerRegion`) recompile Orca's callers.
  Replace the type only in the step that replaces its last user.
- The patch (`patches/orca-2.4.2-wasm.patch`) edits Orca files that stages
  will replace (Print.cpp, Clipper, admesh). When a stage replaces a patched
  file, its patch hunk is deleted from the patch in the same commit, so the
  patch only ever lists code that still runs. The patch applies to a clean
  Orca 2.4.2; check that after each such change (not done in stage 1,
  which touched no patched file).
- Orca is AGPL; every kz file that is copied or adapted from it keeps the
  header saying so (the stubs do). Files written from scratch do not need it,
  but the whole binary is still AGPL.
- Link order vs archives: wasm objects pulled out of an archive are
  chosen per symbol; static initialisers in a replaced Orca file go away with
  it. If a stage replaces a file with registration side effects (the `Fill`
  factory, config `def` tables), check that the symbols it defined are still
  all defined.

## 9. Unverified

- Everything in sections 5 and 7 beyond stage 1 is read from the code, not
  tried. No stage after 1 has been built.
- The timings are one cold run per case on a loaded box. The shares are
  right to a few percent at best. The laps from "Generating support" on
  overlap two threads' work, so those lines are not additive.
- Which infill patterns, wall generators and settings the website can send
  was not checked: the profiles in `eval/profiles` are two; the website's
  UI list lives in Karazas-website, which this work does not read.
- The memory split (26 MB load, 27 MB slice) is RSS after each lap, not a
  heap profile.
- The website's current profiles differ from `eval/profiles` (Benchy gives
  14.54 g with the website's P2S files at the time of writing, 13.95 g with
  `eval/profiles`). The golden grams are the `eval/profiles` numbers, as the
  matrix always was.
- Native Orca differs from the wasm by up to 0.09 g on the matrix (README);
  the gate measures against the wasm, not native.
