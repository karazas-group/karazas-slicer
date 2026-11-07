# Grams without toolpaths: first measurements (2026-11-07)

Probe: `kz/roles_probe.cpp` (KZ_ROLES build) prints the exact mm3 per
extrusion role next to area x layer height estimates from the surfaces
process() leaves. 48 gate cases (no Benchy). 0.1 g is about 80 mm3.

Estimate / exact, per role:

| role | ratio | spread that matters |
|---|---|---|
| top, bottom, internal solid | 1.000-1.003 on flat parts | spheres +7% (about +120 mm3, 0.15 g) |
| walls (slice area minus fill area) | 1.007-1.019 | hollowbox40 1.064 (+700 mm3, 0.85 g): thin walls |
| sparse gyroid at density d | 0.79-1.10 | depends on density and on the part |
| internal bridges | 0.71-0.84 | per part |
| organic supports (island area) | 0.38-0.56 of the area | per part |

So a pure area model is fine for solid infill on flat parts and nothing else:
it misses 0.1 g on most cases.

Sampling sparse infill (gyroid on about 1 layer in k, the rest priced at the
sampled mm3 per mm2; both ends of every sparse run always sampled):

| k | within 0.05 g | outside | worst | gate time |
|---|---|---|---|---|
| exact | 54 | 0 | 0 | 34.3 s |
| 2 | 44 | 10 | 0.24 g (invcone 80%) | 34.0 s |
| 4 | 36 | 18 | 0.24 g (mushroom 80%) | 31.3 s |
| 8 | 38 | 16 | 0.30 g (mushroom 80%) | 31.4 s |

The error doesn't shrink with k=2, so it's a bias (mm3 per mm2 depends on
the size of the sparse area, from anchors and edges), not sampling noise.
At best this saves about 9%.

Where the time goes (27 cases each, clean build):
supports off 8.0 s: infill toolpaths 32%, infill regions 20%, walls 18%.
supports on 27.3 s: support generation 66%. Inside organic supports
(15.6 s): drawing the branches 8.5 s, branch pathing 4.4 s, collision
precalc 2.6 s, tips 1.6 s, support toolpaths 0.56 s.
