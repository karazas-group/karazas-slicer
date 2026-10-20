# OrcaSlicer libslic3r to WebAssembly: feasibility spike

Date: 2026-10-19
Orca version: **v2.4.2**, commit `8500fcdccaa10b5099ac20d252af3a7c560046f1` (2026-07-06), shallow clone at `/home/user/orca-wasm/orca`
Toolchain: Emscripten 6.0.13 (emsdk at `/home/user/orca-wasm/emsdk`), Node 22.22 as the test host
All work is in `/home/user/orca-wasm` (outside any repo). Nothing was committed or pushed.

---

## 0. Verdict

**Feasible. A wasm build was produced and it matches native output.** The probe links unmodified Orca 2.4.2 libslic3r plus a 70-line driver into one `.wasm`. It loads the flattened P2S and Ender-3 S1 profiles, slices with walls, top/bottom, sparse infill and organic tree supports at 35°, writes G-code, and runs GCodeProcessor.

- **Accuracy:** all 28 reference cases slice. On 24, grams are **identical** to the native OrcaSlicer 2.4.2 CLI; three differ by 0.01 g and one (Benchy, S1, organic) by 0.15 g (0.7%). Filament mm agrees within ±0.05%. Model print time agrees within 11 s on the cases where native reports it.
- **Fixed Oct 20:** `sphere40` and `benchy` used to crash in Arachne with "memory access out of bounds". It was a 32-bit `size_t` bug in Orca's own code. See §8.
- **Still open:** wasm wall time is about 2 s minimum, and about 3.5-4.5x native for organic-support jobs.
- **Size:** 8.4 MB raw, **2.66 MB gzip**, before any trimming.
- **Effort:** about **3-5 engineer-weeks** to reach a production-quality minimal port (§10).
- **Shortcut:** the existing `three-slicer` project (§3) has already done a similar port.

---

## 1. Third-party dependencies

The table covers what `src/libslic3r/CMakeLists.txt` links (lines ~580-620), plus root `CMakeLists.txt:584` for the Boost components and the `deps/` versions.

| Dependency (Orca deps ver.) | Used by (libslic3r) | Needed for slice+support+estimate? | Emscripten status | Decision | Notes from this spike |
|---|---|---|---|---|---|
| **Boost 1.84** headers | everywhere (polygon/voronoi, spirit, format, algorithm…) | yes | header-only works | **KEEP** | `deps-src/boost-1.84.0` |
| Boost.filesystem / thread / nowide / locale / date_time / chrono / atomic / container | Utils, Config, Thread, LocalesUtils, PlaceholderParser | yes (small) | built with b2/CMake + emcmake; OK | **KEEP** | `bwasm/stage/lib`. Boost's CMake build fails on `boost_context` i386 `.S`, so build with `ninja -k 0` or select components |
| Boost.Log | ~80 files (`BOOST_LOG_TRIVIAL`) | no (logging only) | compiles by hand (30 TUs, `-DBOOST_LOG_WITHOUT_{SYSLOG,IPC,EVENT_LOG,DEBUG_OUTPUT} -DBOOST_LOG_USE_STD_REGEX`) | **KEEP now, STUB later** | A header shim (`BOOST_LOG_TRIVIAL(x)` mapped to a null stream) saves about 300-500 KB. three-slicer does this |
| Boost.regex / iostreams / program_options / log_setup | GUI/CLI side | no | – | **CUT** | not referenced by reachable objects |
| **oneTBB** (Orca: 2021.5) | parallel_for ×136, parallel_pipeline ×8 (GCode export), concurrent_vector, spin_mutex, task_group, enumerable_thread_specific, task_arena, global_control | yes | **oneTBB master supports emscripten** (af14bb5 used). Builds with `-pthread -fwasm-exceptions` | **KEEP** (MT) or serial build (ST) | Must be built with the same EH model (`-fwasm-exceptions`), otherwise `__resumeException` is undefined at link |
| tbbmalloc (`scalable_allocator`) | Point.hpp, clipper.hpp, SupportLayer.hpp | no | – | **STUB** | `probe/driver/wasm_shims.cpp` forwards `scalable_*` to malloc. Consider `-sMALLOC=mimalloc` |
| **CGAL 5.6.3** (header-only) | `Geometry/VoronoiUtilsCgal.cpp` (Epeck, planarity check used by Arachne and MedialAxis), Triangulation, CutSurface, MeshBoolean | **yes**, Voronoi validation in Arachne | header-only works with `-DCGAL_DISABLE_GMP=1 -DCGAL_DISABLE_ROUNDING_MATH_CHECK` | **KEEP (headers), no GMP/MPFR** | MeshBoolean.cpp needs `Epeck_with_sqrt` (LEDA/CORE), which is cut and not reachable |
| GMP / MPFR | CGAL exact kernels | no (CGAL falls back to MP_Float) | ports exist but are unnecessary | **CUT** | |
| **Eigen 5.0.1** | everywhere | yes | header-only, fine | **KEEP** | |
| **Clipper (deps_src) / Clipper2** | everywhere | yes | compiles | **KEEP** | |
| **admesh** | TriangleMesh/STL | yes | compiles | **KEEP** | |
| **qhull 8.0.2** | ConvexHull (Geometry), supports | yes | builds with emcmake. **C code needs `-sSUPPORT_LONGJMP=wasm`**, otherwise `emscripten_longjmp` is undefined | **KEEP** | `deps-src/qhull/bwasm2` |
| libigl (headers) | TriangleMesh utils, MeshBoolean | partly | header-only | **KEEP headers** | |
| libnest2d (headers) | Arrange, TreeSupport.cpp (EdgeCache only), MinAreaBoundingBox | TreeSupport header use only | header-only | **KEEP headers** | |
| NLopt 2.5 | `Optimize/NLoptOptimizer.hpp`, Arrange.cpp, SLA/SupportTree*, SLA/Rotfinder | **no** | would build, unnecessary | **CUT** | the 4 TUs that need it are unreachable |
| cereal 1.3.0 | Model/Config/TriangleMesh serialization | yes (compiled in, header-only) | header-only | **KEEP** | |
| miniz | 3MF/zip | only for 3MF input | compiles | **KEEP** (if 3MF input), else CUT | |
| expat | 3mf.cpp, bbs_3mf.cpp, AMF.cpp | only for 3MF input | compiles (`deps_src/expat`) | **KEEP** (3MF) / CUT | |
| libnoise | `Feature/FuzzySkin/FuzzySkin.cpp` | no (fuzzy skin off in both profiles), but referenced by PerimeterGenerator | compiles (35/36 files) | **KEEP** (tiny) or ifdef FuzzySkin | |
| semver, qoi, glu-libtess, fast_float, nlohmann, ankerl, earcut | misc | yes (small) | compile | **KEEP** | |
| zlib, libpng | PNGReadWrite.cpp, EdgeGrid debug, Thumbnails | no | emscripten ports exist (`-sUSE_ZLIB/-sUSE_LIBPNG`) but **port download returned HTTP 403 through this sandbox's proxy**, so git-cloned headers were used | **KEEP headers**, stub PNG or cut thumbnails | currently unresolved at link because the code paths are dead |
| libjpeg-turbo | GCode/Thumbnails.cpp | no | – | **STUB** (`SLIC3R_NO_JPEG` patch, falls back to PNG) | |
| OpenSSL (MD5) | Utils.hpp `bbl_calc_md5` (3MF project hashing) | no | – | **STUB** | `probe/stubs/openssl/md5.h` + shim |
| **OCCT 7.6** | Format/STEP.cpp, Format/svg.cpp, Shape/TextShape.cpp; Model.hpp includes STEP.hpp | no | huge; no maintained port | **CUT** | needs the `SLIC3R_NO_STEP` patch in Model.hpp/Model.cpp |
| OpenCV 4.6 | ObjColorUtils.hpp only | no | port exists, huge | **CUT** | |
| draco | Format/DRC.cpp | no | – | **CUT** | |
| OpenVDB (+blosc/OpenEXR) | OpenVDBUtils, SLA/Hollowing; TreeSupport3D only under `#ifndef TREE_SUPPORT_ORGANIC_NUDGE_NEW` (defined = 1) | **no** (organic support does not use it) | very hard | **CUT** | Adaptive/Lightning fill do *not* need OpenVDB |
| mcut | MeshBoolean (libslic3r_cgal) | no | – | **CUT** | |
| freetype / fontconfig | Emboss/TextShape (text on models) | no | – | **CUT** | Emboss.cpp still links, pulled by Model.cpp; ifdef it out |
| CURL / OpenSSL (net) | slic3r GUI only | no | – | **CUT** | |

---

## 2. Reachable code, and what can be cut

The spike built a driver (`/home/user/orca-wasm/probe/driver/wasm_driver.cpp`) that calls the real pipeline:
`load_stl → Print::apply → validate → Print::process() → Print::export_gcode(path, &GCodeProcessorResult) → print_statistics()`.

Object-level reachability came from `probe/reach.py`, which runs llvm-nm over all compiled objects and mimics static-lib pulling. Output is in `probe/reach.txt`.

**186 objects are pulled. These are the libslic3r ones (keep list):**
- **Core:** libslic3r, utils, Config, PrintConfig, Print, PrintApply, PrintBase, PrintObject, PrintObjectSlice, PrintRegion, Layer, LayerRegion, Slicing, SlicingAdaptive, Flow, Extruder, Surface(Collection), ExPolygon, Polygon, Polyline, MultiPoint, Line, Point, BoundingBox, ClipperUtils, Clipper2Utils, TriangleMesh, TriangleMeshSlicer, TriangleSelector, Tesselate, ShortestPath, ShortEdgeCollapse, MutablePolygon, EdgeGrid, ElephantFootCompensation, BridgeDetector, Brim, BuildVolume, PlaceholderParser, LocalesUtils, Thread, Time, ObjectID, CustomGCode, Model, Geometry, Geometry/{Circle, ConvexHull, MedialAxis, Voronoi, VoronoiOffset, VoronoiUtils, VoronoiUtilsCgal}, Algorithm/{LineSplit, RegionExpansion}, MinimumSpanningTree, NormalUtils, PrincipalComponents2D, TriangleSetSampling, FaceDetector, ContourZ, Circle, ArcFitter, VariableWidth, calib, ParameterUtils, MaterialType.
- **Walls:** PerimeterGenerator, Arachne/* (SkeletalTrapezoidation, WallToolPaths, BeadingStrategy/*, utils/*), Feature/FuzzySkin, Feature/Interlocking/*, MultiMaterialSegmentation.
- **Infill:** Fill/* (Fill, FillBase, Rectilinear, Concentric, ConcentricInternal, Gyroid, Honeycomb, 3DHoneycomb, Adaptive, Lightning/*, PlanePath, Line, CrossHatch, TpmsD, TpmsFK).
- **Supports:** Support/{SupportCommon, SupportMaterial, SupportSpotsGenerator, TreeModelVolumes, TreeSupport, TreeSupport3D}, SLA/IndexedMesh (AABB raycasts).
- **G-code and estimate:** GCode, GCodeWriter, GCodeReader, GCode/{GCodeProcessor, CoolingBuffer, SeamPlacer, AvoidCrossingPerimeters, RetractWhenCrossingPerimeters, SpiralVase, FanMover, PressureEqualizer, AdaptivePA*, PchipInterpolatorHelper, SmallAreaInfillFlowCompensator, ToolOrdering, ToolOrderUtils, WipeTower, WipeTower2, ConflictChecker, PrintExtents, TimelapsePosPicker, ThumbnailData}, FilamentGroup, FilamentGroupUtils, FlushVolPredictor.
- **Pulled only because of `Model.cpp`'s generic file I/O (these are removal candidates):** Format/{3mf, bbs_3mf, AMF, OBJ, objparser}, Preset, PresetBundle, AppConfig, Emboss, NSVGUtils, IntersectionPoints, ExPolygonsIndex, Triangulation, PNGReadWrite, SVG.

**Not pulled, so they can be cut from the build list:** SLAPrint, SLAPrintSteps and most of SLA/*, Arrange/ModelArrange, CutUtils, CutSurface, MeshBoolean, Measure, Orient, MinAreaBoundingBox, QuadricEdgeCollapse, Format/{SL1, ZipperArchiveImport, STEP, svg, DRC}, Shape/TextShape, Zipper, GCode/PostProcessor, Geometry/ArcWelder, ProjectTask, TriangleMeshDeal, TryCatchSignal, FlushVolCalc, Semver.cpp, Timer, ExtrusionSimulator, AABBMesh, SlicesToTriangleMesh, TriangulateWall, PolygonTrimmer, ObjColorUtils, BlacklistedLibraryCheck, and the libnoise model/module extras.

About 664 symbols are "missing" in the closure: OCCT, OpenCV, nlopt, draco, png, jpeg and similar. **wasm-ld does not complain about them because their call sites are dead.** The link succeeds with no stub libraries for those.

Subsystem sizes (lines): Support 18.6k, GCode 29.3k, Fill 15.3k, Format 18.2k, Arachne 7.4k, SLA 7.9k, Geometry 5.9k. libslic3r totals 256k lines, and about 60% of it ends up in the binary.

**Standalone overhang / auto-support detection:** `TreeSupport::detect_overhangs(bool check_support_necessity)` (`src/libslic3r/Support/TreeSupport.cpp:668`) can be called after `PrintObject::slice()` without generating supports. `SupportSpotsGenerator` (already linked) is the alternative. Both can be exposed as a cheap "needs supports?" pre-check. For organic style the dispatch is at `Support/TreeSupport.cpp:1728`, where `smsTreeOrganic` goes to `generate_tree_support_3D`.

---

## 3. Existing work

| Project | What it is | Maturity | What they cut |
|---|---|---|---|
| **three-slicer** (npm `three-slicer` 0.3.5, 2026-10-16; GitHub `kimgh06/Three_Slicer`, inspected at 917b79d, cloned to `/home/user/orca-wasm/ext/ts-src`) | Custom wasm kernel that **compiles verbatim Orca TUs** (Arachne, Fill/*, TreeSupport3D/organic, FillLightning, Config/PrintConfig, GCodeProcessor, WipeTower, PlaceholderParser, CoolingBuffer) behind its own driver (`params.cpp`, 159 wired params). It also includes the PrusaSlicer 2.9.6 SLA chain | About 2 months old, single author, AGPL. ST build (SINGLE_FILE) and MT build (pthreads, pool = hardwareConcurrency, 4 GB max, 2 MB stacks). MT wasm is 6.0 MB / 1.9 MB gzip | Stubs tbb (serial), boost/log, boost/thread, nowide, filesystem, cereal. Uses `CGAL_DISABLE_GMP`, `-fwasm-exceptions`, `-O2` (-O3 gave +0.7% speed for +9% size; `-flto` crashes wasm-ld). **Own driver, not `Print::process`**. Not all keys are wired, no `;TYPE:` comments, and its docs report about 25% time-estimate drift between its ST and MT kernels |
| **cura-wasm** (CuraEngine 4.6) | Emscripten build of CuraEngine | About 4 years stale | No OpenMP. 2.3 s native vs 6.6 s browser on their benchmark (about 2.9x) |
| SimplyPrint, Kiri:Moto | SimplyPrint runs Orca **server-side**. Kiri:Moto is a native-JS slicer | – | – |
| PrusaSlicer/libslic3r wasm | **No public port found** | – | – |

**Conclusion:** the approach in this spike, running the real `Print::process()` and `GCode::do_export`, is closer to native parity than three-slicer. This spike reproduces Orca's grams exactly because it uses Orca's own config resolution and G-code pipeline. three-slicer is useful as a source of build tricks (stub set, flags) but not as a base.

---

## 4. What was actually built (real compile and link results)

### 4.1 TU census

All 208 libslic3r TUs were compiled with em++ (`probe/cc.sh`, flags below). Results are in `probe/results.txt` and `probe/results2.txt`, and logs in `probe/logs/`.

- **197 of 208 compile** (after the 6 small patches in §4.2). This includes Print, PrintObject, PrintConfig, GCode, GCodeProcessor, TreeSupport, TreeSupport3D, all of Arachne and Fill, Format/STL, 3mf, bbs_3mf, AMF and OBJ.
- **11 do not compile.** All are unreachable and cut:

| TU | First error |
|---|---|
| Arrange.cpp, SLA/SupportTree.cpp | `libnest2d/optimizers/nlopt/nlopt_boilerplate.hpp:9: 'nlopt.hpp' file not found` |
| SLA/SupportTreeBuildsteps.cpp, SLA/Rotfinder.cpp | `Optimize/NLoptOptimizer.hpp:9: 'nlopt.h' file not found` |
| Format/DRC.cpp | `'draco/compression/encode.h' file not found` |
| Format/STEP.cpp | `Format/STEP.hpp:3: 'XCAFDoc_DocumentTool.hxx' file not found` |
| Format/svg.cpp | `'BRepBuilderAPI_MakeWire.hxx' file not found` |
| Shape/TextShape.cpp | `'Standard_TypeDef.hxx' file not found` |
| ObjColorUtils.cpp | `ObjColorUtils.hpp:5: 'opencv2/opencv.hpp' file not found` |
| SLA/Hollowing.cpp | `OpenVDBUtils.hpp:11: 'openvdb/openvdb.h' file not found` |
| MeshBoolean.cpp | `CGAL/Exact_predicates_exact_constructions_kernel_with_sqrt.h:27: "You need LEDA or CORE installed."` |

### 4.2 Source patches needed

`cd /home/user/orca-wasm/orca && git diff` shows them all (24 lines):

1. **`src/libslic3r/Model.hpp`**: `#include "Format/STEP.hpp"` is wrapped in `#ifndef SLIC3R_NO_STEP`, with forward declarations of `Step`, `ImportStepProgressFn` and `StepIsUtf8Fn` in the `#else`. Model.hpp otherwise drags OCCT into every TU.
2. **`src/libslic3r/Model.cpp`**: the body of `Model::read_from_step` is put under `#ifdef SLIC3R_NO_STEP throw RuntimeError(...)`.
3. **`src/libslic3r/GCode.hpp:179`**: `make_nop_layer_result()` now uses `size_t(std::numeric_limits<coord_t>::max())`.
   - Error was: *"constant expression evaluates to 9223372036854775807 which cannot be narrowed to type 'size_t'"*.
   - Cause: **wasm32 has a 32-bit size_t.**
4. **`src/libslic3r/LocalesUtils.cpp`**: added `#include <sstream>`.
   - Error was: *"implicit instantiation of undefined template std::basic_stringstream"*.
   - Cause: libc++ is stricter about transitive includes.
5. **`src/libslic3r/Platform.cpp`**: added an `#elif defined(__EMSCRIPTEN__)` branch that sets Platform::Unknown.
   - Error was: `static_assert "Unknown platform detected"`.
6. **`src/libslic3r/GCode/Thumbnails.cpp`**: `#ifndef SLIC3R_NO_JPEG` around the jpeglib includes. `compress_thumbnail_jpg` falls back to PNG.

### 4.3 Driver-side fixes found while running

Both are in `probe/driver/wasm_driver.cpp`.

- **`Print::m_origin` (`src/libslic3r/Print.hpp:1192`) is an uninitialised `Vec3d`.**
  - The GUI sets it via PartPlate (`slic3r/GUI/PartPlate.cpp:2398`). The bare library never does.
  - In wasm the field held garbage (NaN), so **every X/Y in the G-code came out as `-9223372036854775.808`**, which is `int64_t(NaN)` from `GCodeFormatter::emit_axis`. GCodeProcessor then reported `20 mm` and `1006467392d` of print time.
  - Fix: `print.set_plate_origin(Vec3d::Zero())`.
  - Lesson: **uninitialised members that "happen to be zero" natively will surface in wasm.**
- **oneTBB worker stacks default to 64 KB on emscripten** (`onetbb/cmake/compilers/Clang.cmake:21`, `__TBB_EMSCRIPTEN_STACK_SIZE`). `DEFAULT_PTHREAD_STACK_SIZE` does not apply because TBB passes an explicit size.
  - Fix: `tbb::global_control(thread_stack_size, 4 MB)` in main(). Alternatively, rebuild TBB with `-DTBB_EMSCRIPTEN_STACK_SIZE=4194304`.

### 4.4 Link

`probe/link.sh` links driver + shims + `libslic3r_all.a` + `libdeps.a` (admesh, clipper, clipper2, semver, miniz, glu-libtess, libnoise, qoi, expat) + `libboostlog.a` + Boost {filesystem, thread, locale, nowide, date_time, chrono, atomic, container} + libtbb.a + qhull. It succeeds in about 23 s.

Link errors hit on the way, all fixed:
- `__resumeException` ×64: TBB had been built with JS exceptions. Rebuilt with `-fwasm-exceptions`.
- `emscripten_longjmp` ×7: qhull C code. Rebuilt with `-sSUPPORT_LONGJMP=wasm`.
- `pthread_setname_np`: missing from emscripten libc. Shimmed.
- `-Wl,-error-limit=0` is mis-parsed; use `-Wl,--error-limit=0`.

**Output:** `probe/out/orca_probe.wasm` is **8,417,750 bytes, 2,663,233 bytes gzip -9**. The JS glue is 98 KB.

### 4.5 Run results vs native OrcaSlicer 2.4.2 CLI

Native reference: `/home/user/slicer-eval/orca/orca-results.txt`. wasm results: `probe/run/batch.txt`, G-code in `probe/run/*.gcode`.

- Profiles: the same flattened JSONs from `/home/user/slicer-eval/orca/profiles/{p2s,s1}-{machine,process,pla}.json`.
- `org35` means `enable_support=1 support_type=tree(auto) support_style=organic support_threshold_angle=35`.
- "time" is the `model printing time` header, which is the same field `run.py` reports.
- wasm ran in Node 22 with 4 TBB threads (PTHREAD_POOL_SIZE=4) on a 4-core container.

| model | case | native g | **wasm g** | native mm | wasm mm | Δmm | native time | wasm time | native s* | wasm s** |
|---|---|---|---|---|---|---|---|---|---|---|
| cube20 | p2s-s0 | 3.55 | **3.55** | 1189.09 | 1189.7 | +0.05% | 21m 1s | 20m 59s | 0.4 | 2.1 |
| cube20 | p2s-org35 | 3.55 | **3.55** | 1189.09 | 1189.7 | +0.05% | 21m 1s | 20m 59s | 0.4 | 2.1 |
| cube20 | s1-s0 | 3.97 | **3.97** | 1330.76 | 1330.6 | -0.01% | – | 23m 22s | 0.4 | 2.1 |
| cube20 | s1-org35 | 3.97 | **3.97** | 1330.76 | 1330.6 | -0.01% | – | 23m 22s | 0.4 | 2.4 |
| invcone | p2s-s0 | 4.98 | **4.98** | 1669.15 | 1669.1 | 0.00% | 20m 30s | 20m 31s | 1.5 | 2.2 |
| invcone | p2s-org35 | 8.11 | **8.11** | 2719.33 | 2720.5 | +0.04% | 34m 28s | 34m 39s | 5.8 | 20.9 |
| invcone | s1-s0 | 6.00 | **6.00** | 2012.82 | 2012.1 | -0.04% | – | 31m 46s | 1.7 | 2.6 |
| invcone | s1-org35 | 12.00 | **12.00** | 4022.85 | 4023.1 | +0.01% | – | 1h 31m 59s | 5.9 | 25.4 |
| mushroom | p2s-s0 | 6.30 | **6.30** | 2112.89 | 2112.4 | -0.02% | 29m 32s | 29m 32s | 0.5 | 2.1 |
| mushroom | p2s-org35 | 12.24 | **12.24** | 4102.99 | 4102.5 | -0.01% | 56m 27s | 56m 28s | 3.1 | 10.4 |
| mushroom | s1-s0 | 7.67 | **7.67** | 2571.55 | 2571.6 | 0.00% | – | 51m 31s | 0.5 | 2.3 |
| mushroom | s1-org35 | 17.73 | **17.73** | 5944.64 | 5944.7 | 0.00% | – | 2h 49m 55s | 3.3 | 13.0 |
| bracket | p2s-s0 | 6.22 | **6.22** | 2087.04 | 2086.7 | -0.02% | 36m 49s | 36m 49s | 0.5 | 2.3 |
| bracket | p2s-org35 | 6.22 | **6.22** | 2087.04 | 2086.7 | -0.02% | 36m 49s | 36m 49s | 0.6 | 2.1 |
| bracket | s1-s0 | 7.33 | **7.33** | 2458.33 | 2458.3 | 0.00% | – | 53m 31s | 0.5 | 2.3 |
| bracket | s1-org35 | 7.33 | **7.33** | 2458.33 | 2458.3 | 0.00% | – | 53m 31s | 0.4 | 2.3 |
| cup | p2s-s0 | 16.03 | **16.03** | 5372.99 | 5373.1 | 0.00% | 51m 7s | 50m 59s | 3.8 | 4.9 |
| cup | p2s-org35 | 16.03 | **16.03** | 5372.99 | 5373.1 | 0.00% | 51m 7s | 50m 59s | 3.9 | 5.1 |
| cup | s1-s0 | 16.04 | **16.04** | 5379.13 | 5377.9 | -0.02% | – | 1h 15m 48s | 4.8 | 10.3 |
| cup | s1-org35 | 16.04 | **16.04** | 5379.00 | 5377.9 | -0.02% | – | 1h 15m 48s | 4.4 | 10.1 |
| sphere40 | p2s-s0 | 10.87 | **10.87** | 3642.90 | 3643.7 | 0.02% | 36m 5s | | | 4.7 |
| sphere40 | p2s-org35 | 11.52 | 11.53 | 3863.57 | 3865.3 | 0.04% | 37m 37s | | | 9.3 |
| sphere40 | s1-s0 | 12.02 | **12.02** | 4031.69 | 4031.7 | 0.00% | – | | | 6.4 |
| sphere40 | s1-org35 | 13.32 | 13.31 | 4464.45 | 4463.9 | -0.01% | – | | | 11.2 |
| benchy | p2s-s0 | 11.15 | **11.15** | | 3736.8 | | | | | 14.4 |
| benchy | p2s-org35 | 15.86 | **15.86** | | 5318.1 | | | | | 22.5 |
| benchy | s1-s0 | 12.58 | 12.59 | | 4222.5 | | | | | 14.6 |
| benchy | s1-org35 | 20.31 | 20.46 | | 6861.5 | | | | | 28.0 |

The sphere40 and benchy rows are from the run after the Arachne fix (`results/wasm-parity.txt`); native mm for benchy is in `results/orca-native.txt`.

\* Native ms comes from run.py and includes AppImage start and 3MF export.
\** wasm wall time includes Node start, compiling 8.4 MB of wasm, and `full_print_config()`. That fixed floor is about 1.7-2 s.

Notes on the results:
- **Grams match exactly on 24 of 28.** sphere40 with organic supports and benchy S1 without supports differ by 0.01 g; benchy S1 organic by 0.15 g. The wasm build gives the same numbers on every run, so this is either native varying between runs (organic supports run in parallel) or a small real difference. Re-run native to tell which.
- The orca-results.txt S1 rows have no time because the run.py regex found no time for S1, so there is nothing to compare. The wasm S1 header times look plausible.
- `orca_slice()` currently returns `modes[Normal].time`, which is the **total** estimated time. For the P2S that includes about 9.5 min of start-G-code prep (`cube20`: model 20m59s, total 30m33s). The shop must pick one number deliberately. `run.py` reports the "model printing time".
- Harmless P2S log line: `update_values_to_printer_extruders_for_multiple_filaments: option filament_retract_lift_enforce not found`.

---

## 5. Build approach (recommended for the real port)

1. **Toolchain:** `emcmake cmake` with a dedicated `cmake/wasm/` superbuild. Do not reuse Orca's `deps/` superbuild, which builds OCCT, OpenCV, OpenVDB and others.
   - Compile flags: `-std=c++17 -O2 -pthread -fwasm-exceptions` (C: `-sSUPPORT_LONGJMP=wasm`).
   - Defines: `-DNDEBUG -DUSE_TBB -DTBB_USE_CAPTURED_EXCEPTION=0 -DBOOST_ALL_NO_LIB -DSLIC3R_NO_STEP -DSLIC3R_NO_JPEG -DCGAL_DISABLE_GMP=1 -DCGAL_DISABLE_ROUNDING_MATH_CHECK`.
   - **Every object, including TBB, qhull and Boost, must use the same EH and longjmp model.**
   - Use wasm EH (`-fwasm-exceptions`, supported in Chrome 95+, Firefox 100+, Safari 15.2+), not JS EH (slower and larger).
2. **Dependencies:**
   - Pin git clones or release assets. In this sandbox the emscripten-ports downloads (GitHub `archive/` codeload) returned 403, while git clone and release assets worked.
   - Header-only: Boost, Eigen, cereal, CGAL, libigl, libnest2d, nlohmann, fast_float.
   - Built as static libs: TBB (`-DTBB_STRICT=OFF -DTBBMALLOC_BUILD=OFF -DTBB_TEST=OFF`), qhull, Boost {filesystem, thread, nowide, locale, date_time, chrono, atomic, container} (skip `context`/`coroutine`/`fiber`, whose asm fails), and deps_src {admesh, clipper, clipper2, semver, miniz, glu-libtess, expat, qoi, libnoise}.
3. **libslic3r:**
   - Patch `src/libslic3r/CMakeLists.txt` with an `if(EMSCRIPTEN)` source filter that drops the cut list in §2 and the OCCT, OpenCV, draco, OpenVDB, mcut, NLopt and freetype link lines.
   - Apply the 6 patches in §4.2, ideally as upstreamable `#ifdef`s.
   - Then add `SLIC3R_WASM_MINIMAL` ifdefs in `Model.cpp`'s `read_from_file` and `read_from_archive` so Format/3mf (unless wanted), bbs_3mf, AMF, OBJ, Preset, PresetBundle, AppConfig and Emboss drop out.
4. **Link:**
   ```
   -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=256MB -sMAXIMUM_MEMORY=2GB
   -sSTACK_SIZE=4MB -sDEFAULT_PTHREAD_STACK_SIZE=4MB
   -sPTHREAD_POOL_SIZE=navigator.hardwareConcurrency -sPROXY_TO_PTHREAD (or run in a Web Worker)
   -sMODULARIZE -sEXPORT_ES6 -sENVIRONMENT=web,worker
   -sEXPORTED_FUNCTIONS=[_orca_slice,...]  (or embind)
   --closure 1  (JS glue)
   ```
   Drop `-sNODERAWFS`, which is for test use only. Use MEMFS and pass the STL bytes in.
5. **API:**
   - `orca_slice(stl_bytes, printer_id, filament_id, overrides_json)` returns `{grams, mm, model_time_s, total_time_s, needs_support}`.
   - Write G-code to MEMFS, or to `/dev/null` with GCodeProcessor fed in-process. The G-code text is not needed, and skipping it saves memory.
   - Ship the 4 flattened profile JSONs (P2S + S1 × PLA + PETG, plus the 0.20mm process) as static assets. They are about 10-20 KB each.

---

## 6. Threading plan

**Recommended: real pthreads (TBB on Web Workers) with COOP/COEP. This is what the probe already runs.**

- **Headers.** Add to the existing Cloudflare Pages `_middleware.js`:
  ```
  Cross-Origin-Opener-Policy: same-origin
  Cross-Origin-Embedder-Policy: require-corp    (or 'credentialless'; Safari does not support credentialless)
  ```
  - This makes `crossOriginIsolated === true`, so SharedArrayBuffer is available.
  - **Side effect:** every cross-origin subresource on the page (payment widgets, analytics, fonts/CDN, YouTube embeds) must send CORP/CORS or it is blocked.
  - Mitigation: scope the headers to the pricing route only (check `url.pathname` in the middleware), or host the slicer page as its own isolated route.
  - Fallback: if `crossOriginIsolated` is false, load the single-thread build.
- **Run the module in a dedicated Web Worker** (or with `-sPROXY_TO_PTHREAD`).
  - The main thread must never block, because emscripten can only spawn new workers while the main thread is free.
  - Size `PTHREAD_POOL_SIZE` to `navigator.hardwareConcurrency` and **cap TBB to the pool** with `tbb::global_control(max_allowed_parallelism, pool)`.
  - Observed in the spike: setting `max_allowed_parallelism=64` with a pool of 4 **hung forever**. TBB tried to start more workers than were pre-spawned, and `pthread_create` waited on a blocked main thread.
- **TBB worker stack:** set 4 MB via `global_control(thread_stack_size)`, or rebuild TBB with `TBB_EMSCRIPTEN_STACK_SIZE`. The default of 64 KB silently corrupts memory in deep code (Arachne, organic support).
- **pthreads + ALLOW_MEMORY_GROWTH** gives a link warning ("may run non-wasm code slowly"). This is fine: the hot code is wasm. Alternatively, `-sGROWABLE_ARRAYBUFFERS`.
- **Single-thread fallback:**
  - Option 1: oneTBB built with `-DEMSCRIPTEN_WITHOUT_PTHREAD`. It runs tasks inline and needs no code changes.
  - Option 2: a 100-line serial TBB shim (three-slicer's approach).
  - No COOP/COEP needed, simpler hosting, smaller glue.

**Speed (measured and estimated):**
- Measured, 4-thread wasm in Node vs native Orca on the same 4-core box:
  - Light jobs: about 2.1-2.6 s, dominated by about 1.7-2 s of fixed start-up. In a browser with a cached, pre-compiled module (`WebAssembly.compileStreaming` plus keeping the worker alive), the fixed cost is paid once per page view, not per quote.
  - Organic-support jobs: **3.4-4.3x native** (invcone 20.9 s vs 5.8 s; mushroom 10.4 s vs 3.1 s).
  - cup: 1.3-2.2x.
- Estimate for single-thread wasm: another 2-3.5x on top of 4-thread wasm for support-heavy jobs, which is about 40-80 s for a support-heavy model like invcone-org35.
  - It is acceptable for small parts, but too slow for a pricing page with large models.
  - Recommendation: **ship MT and use ST as the fallback.**
- **Optimisation headroom:**
  - `-O3`/`-msimd128` are worth trying. Eigen and Clipper benefit from SIMD.
  - Skip G-code string building where possible.
  - Profile `TreeSupport3D`. Organic support is where most of the time goes.

---

## 7. Size and memory

- **Measured (untrimmed):** 8.42 MB wasm, **2.66 MB gzip**, plus 98 KB JS. Brotli (Cloudflare serves it automatically) is typically 15-20% smaller, so about 2.2 MB.
- **After trimming** (Model readers, Preset/PresetBundle/AppConfig, Emboss/NSVG, Boost.Log as a null shim, PNG/thumbnail, libnoise): estimate **about 6-7 MB raw, 1.9-2.2 MB gzip, 1.6-1.9 MB brotli**. three-slicer's comparable kernel is 6.0 MB / 1.9 MB gzip.
- **Memory:**
  - The probe starts at 256 MB. Node RSS during sphere40 was about 305 MB.
  - Budget **256 MB initial, growth to 1-2 GB** for big organic-support jobs. three-slicer reports 1.3-1.8 GB peaks with 3-5 workers.
  - Mobile Safari is the constraint: wasm memory above about 1 GB is unreliable on iOS. Cap the pool to 2-4 threads on mobile, and reject or downsample very large meshes (use triangle-count and bbox limits in the UI).

---

## 8. Risks and open issues

1. **Arachne memory access out of bounds. Fixed Oct 20.**
   - Cause: `SkeletalTrapezoidation::interpolate` (inlined into `propagateBeadingsDownward`) runs `for (next_inset_idx = left.toolpath_locations.size() - 1; next_inset_idx >= 0; ...)` with a 64-bit signed `coord_t`. With an empty vector, `size() - 1` is 2^32-1 on wasm32 (32-bit `size_t`), not -1, so the loop reads index 4294967295. On 64-bit native it wraps to -1 and the loop never runs.
   - Fix: `coord_t(left.toolpath_locations.size()) - 1`, now in `patches/orca-2.4.2-wasm.patch`. A grep of libslic3r for the same pattern (`size() - 1` into a loop counter tested `>= 0`) found no other case.
   - The notes below are what was known before the fix.
   - Trace: `SkeletalTrapezoidation::propagateBeadingsDownward` ← `generateSegments` ← `WallToolPaths::generate` ← `FillConcentricInternal::fill_surface_extrusion` ← `Layer::make_fills` ← `PrintObject::infill()` (tbb parallel_for), on a TBB worker.
   - It persists with 4 MB TBB stacks, with `wall_generator=classic` (FillConcentricInternal still uses Arachne), and with fixed 1.5 GB memory and no growth. So it is not stack overflow or memory growth.
   - Likely another wasm32-only UB in the same family as `Print::m_origin`: an uninitialised member, a 32-bit `size_t`/`long` overflow, or OOB indexing that is benign on native 64-bit.
   - Next steps:
     - Build with `-fsanitize=address` (emscripten ASan) or `-sSAFE_HEAP=1`.
     - Build native libslic3r with `-m32` or ASan/UBSan to see whether it is a latent Orca bug.
   - Estimate 1-5 days. **This is the main technical risk.**
2. **wasm32 type-width bugs.**
   - `size_t`/`long` are 32-bit. One compile-time instance has been seen (GCode.hpp), plus the coordinate/origin runtime bug.
   - wasm64 (`-sMEMORY64`) is not an option for Safari.
   - Mitigation: a broad corpus regression against native (see §9).
3. **Config parity for P2S.**
   - P2S uses extruder variants ("Direct Drive Standard / High Flow"), resolved by `update_values_to_printer_extruders` (`src/libslic3r/PresetBundle.cpp:116-149`, `src/OrcaSlicer.cpp`).
   - The flattened JSONs already resolve this, and grams match. Profiles must be **re-flattened** from the same Orca version whenever Orca is upgraded. Do not load the raw inherits chain in the browser.
4. **COOP/COEP collateral** on the pricing page's third-party embeds (§6).
5. **TBB on emscripten** is supported upstream but lightly used. There is a deadlock risk if TBB concurrency exceeds the pthread pool (observed). Keep the cap.
6. **Organic support speed:** 10-25 s in wasm for moderately sized parts. Use progress UI and cancellation (`Print::cancel()` / `throw_if_canceled` already exist) and debounce re-slicing.
7. **Upstream drift:** Orca moves fast. Keep the patch set small and `#ifdef`-only, and pin to a tag.
8. **Exceptions:** wasm EH needs Safari 15.2+. Older browsers fall back to "quote unavailable".

---

## 9. Validating a wasm build against the native CLI

Goal: same profiles produce the same grams.

- **Reference generator.** Use the native Orca 2.4.2 CLI at `/home/user/slicer-eval/orca/app/AppRun` via:
  ```
  xvfb-run -a python3 /home/user/slicer-eval/orca/run.py {p2s|s1} model.stl tag [k=v...]
  ```
  It writes `orca-results.txt` lines `{grams, mm, time}`.
- **Same inputs.** The wasm side loads the **identical flattened JSONs** (`/home/user/slicer-eval/orca/profiles/{p2s,s1}-{machine,process,pla}.json`, plus PETG files to be added) in the same order (machine, process, filament) and applies the same `k=v` overrides.
  - The probe's driver takes them as `node orca_probe.js model.stl "m.json;p.json;f.json" out.gcode key value ...`.
- **Matrix.**
  - Models: cube20, cup, sphere40, invcone, mushroom, bracket, benchy, hollowbox40, plus 20-50 real customer STLs.
  - Printers and filaments: {P2S, Ender-3 S1} × {PLA, PETG}.
  - Supports: {s0, org35}, where org35 = `enable_support=1 support_type=tree(auto) support_style=organic support_threshold_angle=35`.
- **Pass criteria.**
  - grams: exact to 0.01 g, or ≤0.5% on very large prints.
  - filament mm: ≤0.1%.
  - model printing time: ≤1% / ≤30 s.
  - For a deeper check, also diff layer count and per-`;TYPE:` extruded length (support vs. walls vs. infill) from the G-code. The probe matches the native G-code line count to 0.3%.
  - The spike reached exact grams and 0.05% mm on 20/20 completed cases.
- **Automation.** A CI job runs the native CLI once to produce goldens. The wasm build runs under Node (same `.wasm` the browser loads, with a Node-only `NODERAWFS` test harness or MEMFS) and fails on any deviation. Add a Playwright smoke test in Chrome, Firefox and Safari/WebKit for the COOP/COEP plus worker path.
- **Version lock.** Pin to v2.4.2 and the same flattened profiles. When upgrading Orca, upgrade both sides and regenerate goldens.

---

## 10. Step-by-step plan and effort (one engineer familiar with C++/CMake; scoped to P2S + Ender-3 S1, PLA/PETG)

| # | Step | Effort |
|---|---|---|
| 0 | Spike (this). Toolchain, census, link, parity on 20 cases | done |
| 1 | Fix the Arachne OOB crash. **Done Oct 20** (32-bit `size_t` bug, §8). A sweep for other uninitialised members (Print, GCode, PrintObject) is still worth doing | done |
| 2 | Proper `emcmake` superbuild for deps (TBB, qhull, Boost subset, deps_src libs) plus a libslic3r `EMSCRIPTEN` source filter. Reproducible Docker build | 3-4 d |
| 3 | Trim: `SLIC3R_WASM_MINIMAL` ifdefs in Model.cpp (file readers), PresetBundle/AppConfig/Emboss out, Boost.Log null shim, PNG/thumbnail stubs. Re-measure size (target ≤2.2 MB gzip) | 2-3 d |
| 4 | API: C entry point with embind/ccall, MEMFS mesh input (STL binary/ASCII; 3MF mesh-only via Format/3mf, or parse 3MF in JS and pass triangles), profile bundle (4 flattened combos + PETG flatten), result struct (grams, mm, model/total time, needs_support from `detect_overhangs`), cancellation and progress callback | 3-4 d |
| 5 | Threading and hosting: Web Worker wrapper, pool = hardwareConcurrency (capped 4-8, 2 on mobile), TBB caps, ST fallback build, COOP/COEP in `_middleware.js` scoped to the route, audit third-party embeds | 2-3 d |
| 6 | Validation harness (§9): goldens from the native CLI, Node CI runner, extended model corpus, PETG profiles | 2-3 d |
| 7 | Browser QA (Chrome, Firefox, Safari macOS/iOS, Android), memory limits, mesh-size limits, perf tuning (-O3/SIMD, skip G-code text) | 3-4 d |
| | **Total** | **about 17-26 working days (3.5-5 weeks)** |

---

## 11. Files and artefacts from this spike

- Orca clone (v2.4.2, patched): `/home/user/orca-wasm/orca` (`git diff` = the 6 patches)
- emsdk 6.0.13: `/home/user/orca-wasm/emsdk`
- Dependency sources and builds: `/home/user/orca-wasm/deps-src/{boost-1.84.0 (bwasm/stage/lib), onetbb (build-mt2), qhull (bwasm2), eigen-5.0.1, cereal, CGAL-5.6.3, libnoise, zlib, libpng}`; merged headers in `/home/user/orca-wasm/prefix-mt/include`
- Probe: `/home/user/orca-wasm/probe/`
  - `cc.sh`: per-TU compile, with flags
  - `deps.sh`: deps_src libs
  - `link.sh`: link (`link_dbg.sh` adds `--profiling-funcs`)
  - `driver/wasm_driver.cpp`, `driver/wasm_shims.cpp`
  - `stubs/openssl/md5.h`, `gen/libslic3r_version.h`
  - `reach.py`, `reach.txt`: reachability
  - `results*.txt`, `logs/`: TU census
  - `out/orca_probe.{js,wasm}`
  - `run/batch.txt`, `run/*.gcode`: parity runs
- three-slicer, for reference: `/home/user/orca-wasm/ext/ts-src`, `/home/user/orca-wasm/ext/three-slicer/package`
