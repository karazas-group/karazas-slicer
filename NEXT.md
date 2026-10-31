# Next: rebuild and publish the Clipper2 build

Written 2026-10-31 when `rewrite` and `clipper2` were merged into main. Delete
this file once the steps below are done.

## Where things stand

- main has the rewrite's stage 1 (`kz/`, built from `kz/sources.txt`) and
  Clipper2 (`patches/clipper2.patch`, applied on top of
  `patches/orca-2.4.2-wasm.patch`).
- `dist/` is still the old Clipper1 build from 42a5540 (see `dist/BUILD`).
  The website keeps serving that until dist/ changes.
- `eval/golden-mm.txt` is the Clipper2 baseline. A plain `gate.cjs` run
  against the Clipper1 build in dist/ will not match it, and that's expected.
- Checks already passed on the branch build (2026-10-27 to 31):
  - gate 54/54 against the Clipper2 golden;
  - within 0.0345 g of Clipper1 (invcone with supports is the largest);
  - `eval/ab.cjs` 0.767x;
  - `eval/heap.cjs` 256 MB flat over 4 rounds, the same as the live build.

## Steps

1. **Patch.** Start from a clean Orca 2.4.2 tree (`~/orca-wasm/orca`). Never
   commit that tree.
   ```
   git -C ~/orca-wasm/orca checkout -- . && git -C ~/orca-wasm/orca clean -fd src
   patch -p1 -d ~/orca-wasm/orca < patches/orca-2.4.2-wasm.patch
   patch -p1 -d ~/orca-wasm/orca < patches/clipper2.patch
   ```
   Either patch rejecting a hunk is a stop.
2. **Build.** Follow REWRITE.md §12 "Rebuild", steps 3-5. In short:
   - `wasm/cc.sh` for every source, minus `clipper.cpp` and
     `Clipper2Utils.cpp`, plus `ClipperZUtils.cpp`.
   - `wasm/deps.sh`. Then rebuild `libdeps.a` by hand: drop the `clipper_*`
     members and add the `c2_*` objects.
   - `wasm/roots.sh`, then `wasm/link_web.sh`.

   A finished build of the branch is in the old session's scratchpad
   (`c2bbuild/`), but don't count on it surviving.
3. **Gate.** Benchy isn't in the repo; pass it with `BENCHY=`.
   ```
   BENCHY=<path>/benchy.stl node eval/gate.cjs $OUT/out/web/kz-slicer.js
   ```
   It must be 54/54 identical. Any failure is a stop.
4. **Heap and speed.**
   - `node eval/heap.cjs $OUT/out/web/kz-slicer.js 4` should stay at 256
     every round. Growth the dist/ build doesn't show is a stop.
   - `eval/ab.cjs` with the dist/ build as A should come out around 0.77x.
     Run it alone, not next to another slicer job, or the timing is
     meaningless.
5. **Fix publish.sh before using it.**
   - Its BUILD "source" line only hashes `orca-2.4.2-wasm.patch`. Add the
     `clipper2.patch` hash.
   - Its header comment still says to run the gate "on the rewrite branch".
6. **Publish.** Run `bash wasm/publish.sh`, commit `dist/` with the gate,
   heap and timing numbers, and push main.
   - Pushing `dist/**` fires `.github/workflows/website-import.yml` (needs
     WEBSITE_TOKEN). Without the token, the website's own slicer-import
     workflow picks the build up on its next dev push.
   - Either way the build lands on Karazas-website **dev** only.
   - KZ_SLICER_VERSION changes, so the pricer reslices cached models.
7. **Check the website.**
   - Read the grams line (old vs new) in the import commit on dev.
   - Load dev.karazas.pages.dev/3d/pricer, slice a model, and check the
     console has no errors.
8. **Tidy up.**
   - REWRITE.md §12: change "Clipper2 branch (this branch)" to say it's on
     main.
   - Linear KAR-67: comment with the numbers.
   - Delete this file.

## Rules

- Website: dev branch only, never main, and nothing on karazas.com.
- No server deploys without the owner's OK.
- No model IDs in commits. Use the standard Co-Authored-By/Claude-Session
  trailer.
