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

Every step ends with all 54 cases identical to `eval/golden-mm.txt` at full
precision: the filament length in mm as `%.17g`, so the plastic has to be
bit-identical, not just equal to the hundredth of a gram. Nothing is
committed to `rewrite` otherwise.

```
bash wasm/roots.sh                      # compile kz/sources.txt into $OUT/roots
bash wasm/link_web.sh                   # ~35 s, DEST=<name> for a new output dir
node eval/gate.cjs $OUT/out/<name>/kz-slicer.js     # 54 cases, exit 0 only if identical
```

The cases are what the pricer really sends: every model in `eval/models`
plus Benchy (`BENCHY=/path/benchy.stl` or put it in `eval/models/`; it is not
in the repo, 11 MB), with `eval/profiles/karaza.json` (a copy of the
website's `assets/kz/slicer/karaza.json`, the only settings file the pricer
loads) and the pricer's overrides: walls/infill 3/40, 2/15 and 4/80, supports
off and on (organic, 35 deg, 8 interface layers). The golden file was written
on 2026-10-26 by the build at the start of this change (stage 1 plus the
full-precision output) and checked against the website's shipped wasm: all
54 equal at the 0.1 mm the old output printed. A second run gives the same
bits, so threading doesn't make it flaky. `--write` records a new golden
file; only do that when the website's settings or overrides change, from a
build that matches main.

Each later stage should still add a cheaper check of its own output: dump
the stage's result (polygons, paths, volumes as exact integers or `%a`
doubles) from the old and the new build and compare the files. Removing a
union in the organic supports changed nothing at 0.01 g but changed the
plastic on 6 of 28 cases at full precision; that is the kind of thing the
per-stage dump catches early.

When a change can't be bit-identical (Clipper output order in the organic
supports, for one), the owner's limit is 0.001 g on every case:
`node eval/gate.cjs <js> --tol=0.001` passes cases within that and prints
the largest difference. Such a change also needs a visual check of the
supports against native Orca, because grams can hide a support that moved:

```
bash wasm/link_check.sh                                  # validation build, writes G-code
node eval/gcode.cjs $OUT/out/check/kz-slicer.js <model.stl> ours.gcode
python3 eval/native_gcode.py <website>/build/profiles <model.stl> orca.gcode   # eval/app = Orca AppImage extract
python3 eval/supports_view.py ours.gcode ours orca.gcode orca view.png
```

`supports_view.py` draws front, side and top views of both with supports in
red, and prints the support length per file and the most different layer.
Baseline on 2026-10-26, today's build vs native Orca 2.4.2 with the same
settings: Benchy 49562.8 vs 49562.7 mm of support over 185 layers each,
mushroom identical, sphere40 +0.09%, inverted cone +0.37% (Orca's arrange
turns the model a little on its bed, which moves the trees slightly).

The old 28-case matrix over the P2S and S1 printer profiles is retired
(2026-10-26, owner's decision): the slicer no longer knows about printers,
and S1 was the only thing keeping Arachne in.

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

What the pricer sends: `karaza.json` (46 settings that change the plastic,
nothing about the printer) plus overrides for `wall_loops`,
`sparse_infill_density`, `enable_support`, and, with supports, organic tree
supports at 35 deg with 8 interface layers, and a huge `printable_area`.
That is a closed set. Anything outside it can go; `apply.cpp` should reject
a value it doesn't support rather than price it wrong.

Decided by the owner (2026-10-26):
- No printer profiles. One settings file, and the slicer doesn't know what
  printer it is. The P2S and S1 profiles leave the gate.
- Classic walls only. Arachne (`Arachne/`, `perimeters_arachne`, the Voronoi
  code) goes. Classic is also 2-3x faster on round parts (cup 0.4 vs 1.2 s).
- Brim: every part gets one. Today that is `brim_type=outer_only`, 5 mm, in
  `karaza.json`. In the rewrite it is code, not a setting: `skirt_brim.cpp`
  always draws a 5 mm outer brim around each part and has no skirt at all
  (`skirt_loops` is 0). The brim and skirt settings and `Brim.cpp`'s other
  modes (ears, inner, painted, per-support brims) go.
- No G-code and nothing that only serves the printer: speeds, accelerations,
  temperatures, fans, cooling, retraction, z-hop, wipe, seams, machine limits,
  time estimates. One material, no colours (see section 10).

Follows from the closed set (no further decision needed):
- Organic tree supports only. `TreeSupport.cpp` (3786 lines, slim/strong/
  hybrid), `SupportMaterial.cpp` (3309, normal/snug) and the parts of
  `SupportCommon.cpp` only they use go.
- Infill patterns: only the ones `karaza.json` names (gyroid sparse,
  monotonic-line top and bottom) and the solid infill Orca uses
  internally. The rest of `Fill/` (adaptive cubic, lightning, Hilbert,
  Archimedean, TPMS, 3D honeycomb, ...) goes.
- G-code writer, `GCodeProcessor`, cooling buffer, pressure advance, arc
  fitting (`enable_arc_fitting` is 0), seam placement (already out),
  `estimate_curled_extrusions` and `detect_overhangs_for_lift` (already
  skipped).
- Wipe tower and multi-material: `Print::_make_wipe_tower`, `ToolOrdering`,
  `apply_mm_segmentation`, multi-extruder region splitting. Out of the STL
  pricing path, but not deleted: the owner wants the wipe tower and the 3MF
  importer kept as source to read and rebuild as their own modules for 3MF
  imports and colour prints later (section 11). Note the pipeline
  goes through `ToolOrdering` today because `Print::process` and the skirt
  step use it for `tools_for_layer`; replacing that is part of `process.cpp`.
- OBJ/AMF/STEP readers, the preset system, the placeholder parser (already
  stubbed). The 3MF reader is stubbed out of the pricing link too, but its
  source stays for the 3MF module (section 11).
- Sequential printing, raft, spiral vase, `contour_z`, conical overhang, fuzzy
  skin, `slicing_mode` other than regular, adaptive and variable layer
  heights, shared objects, multiple objects and instances.
- `ironing_type` other than none, `only_one_wall_*` modes it doesn't set,
  `infill_combination`, `thick_bridges`, `gap_infill` off.

NOT deletable, because it changes the plastic: anything that edits geometry
or paths before the sum (elephant foot, xy compensation, bridges, gap fill,
extra perimeters, `infill_wall_overlap`, flow ratios) and
`PrintObject::simplify_extrusion_path` (Print.cpp:2598, the "Optimizing
toolpath" laps, 5% of Benchy), which shortens paths at `resolution` 0.012 mm
before the sum. Those are stages to port (`simplify.cpp`), not delete.

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

## 10. Parked ideas

Measured but not shipped, to pick up when the stage they live in is rewritten:
- Edge map counting sort (`TriangleMesh.cpp`, `its_face_edge_ids` /
  `create_edge_map`): two stable counting-sort passes instead of `std::sort`,
  with the old sort as fallback when an edge key shows up 3+ times. Saves
  ~230 ms of thread time on the inverted cone (sort 393 -> 240-266 ms); exact
  on every check, including 19,500 calls compared in-run. Wall time on a
  shared 4-core box: inside the noise. The fallback was never exercised.
  Belongs to `mesh_slice.cpp` and the per-branch slicing in the supports.
- Organic supports, avoidance precalculation (`TreeModelVolumes::precalculate`):
  only 74-88% of the precomputed radius/layer keys are ever asked for. The
  step costs 40-90 ms; computing on demand saves up to ~15% of it, but the
  computation order changes, so it needs the full-precision gate.
- Organic supports, smoothing setup cache (`organic_smooth_branches_avoid_collisions`):
  about 6 ms of per-layer setup.
- Organic supports, the per-tree union in `organic_draw_branches`: tried
  again under the 0.001 g rule on 2026-10-26. Within it (42/54 identical,
  largest 1.2e-5 g), but SLOWER: inverted cone 3.3 -> 5.2 s, sphere40
  1.4 -> 1.8 s (medians of 4 interleaved runs). The per-tree union shrinks
  each tree before the big per-layer union; without it that one union gets
  every overlapping tree raw. Rejected for good.
- Same, the second union before `smooth_outward`: within the limit (largest
  1.7e-4 g), speed change inside the noise (-6% to +4%). Not worth it.
- OrcaSlicer 2.5.0-alpha (Oct 2026), checked against our tree:
  - PR 16183 (simplify steps copied the whole PrintConfig per path): ported
    to the 2.4.2 patch, 54/54 bit-identical, 1-4% faster. simplify.cpp must
    keep reading the config by reference.
  - PR 16113 (skip the curled-extrusion estimate): we already skip it with
    KZ_NO_GCODE.
  - PR 15969 (Clipper1 -> tuned Clipper2, about 35% faster on their bench,
    Benchy 56-73%): not exact. Extrusion moves up to 0.05% without supports
    and organic supports by -0.6% to -2.9%, so tens of grams of difference
    against a 0.001 g limit. Most of the gain is three ideas we could take
    on their own and gate one by one: a node allocator for Clipper's
    internals, the cheaper offset join, and one PolyTree pass per boolean.
  - PRs 16065/16066/16440 (reslice memory leaks with tree supports): not
    needed, the pricer throws the worker away after each slice.

## 11. Later: multicolour

Not now. Orca's 3MF importer and wipe tower stay in the source tree for
this (not deleted, just not linked). Once the generators export colours in
their 3MF files (open in Linear), the pricer could send a 3MF to the slicer and price each colour. In
the module layout that is a separate reader stage (`load_3mf.cpp`) and a
colour/tool split stage in front of `slice`, used only when a 3MF with
colours comes in; STL pricing doesn't load it. Until then everything above
assumes one material.
