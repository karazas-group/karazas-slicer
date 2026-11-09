# Organic supports: what's known (2026-11-08)

dev slices with normal supports (grid). This branch is for making organic
supports fast enough to bring them back.

Inside the organic support step, 27 gate cases with supports, 15.6 s total:

| step | time |
|---|---|
| draw branches (build a mesh per tree, slice it) | 8.5 s |
| branch pathing | 4.4 s |
| collision precalc | 2.6 s |
| tips | 1.6 s |
| support toolpaths | 0.56 s |

Whole support-on slices, same 27 cases: organic 26.5 s, normal grid 12.2 s,
normal snug 13.5 s, tree slim 85 s, tree hybrid 122 s. Organic vs grid
grams differ by up to 4 g (hollowbox40), so organic needs its own golden
file when it comes back.

First idea to try: draw branches as circles per layer instead of building
and slicing a mesh.
