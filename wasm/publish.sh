#!/usr/bin/env bash
# Puts the browser build into dist/ and records where it came from. The
# website's "slicer import" workflow (Karazas-website,
# .github/workflows/slicer-import.yml) picks dist/ up from the website branch
# on its next dev push, minifies the js, slices a few models with the old and
# new build and commits it to dev. Publish from the website branch and push
# dist/ there; nothing else needed. Run link_web.sh first, then the gate
# (eval/gate.cjs; --tol=0.05 on the website branch).
# Usage: publish.sh [path to kz-slicer.js]   (default $OUT/out/web/kz-slicer.js)
W=${KZ_WASM:-$HOME/kz-wasm}; HERE=$(cd "$(dirname "$0")" && pwd); OUT=${OUT:-$W/build}
js=${1:-$OUT/out/web/kz-slicer.js}; dir=$(dirname "$js"); repo=$(dirname "$HERE")
[ -f "$js" ] && [ -f "$dir/kz-slicer.wasm" ] || { echo "no build at $dir"; exit 1; }
mkdir -p "$repo/dist"
cp "$js" "$dir/kz-slicer.wasm" "$repo/dist/"
{
  echo "built:  $(date -u +%Y-%m-%dT%H:%MZ)"
  printf 'source: %s' "$(git -C "$repo" rev-parse --short HEAD)"
  for p in "$repo"/patches/*.patch; do printf ' + patches/%s %s' "$(basename "$p")" "$(sha256sum < "$p" | cut -c1-12)"; done
  echo
  echo "wasm:   $(sha256sum < "$repo/dist/kz-slicer.wasm" | cut -c1-12), $(wc -c < "$repo/dist/kz-slicer.wasm") bytes"
} > "$repo/dist/BUILD"
cat "$repo/dist/BUILD"
