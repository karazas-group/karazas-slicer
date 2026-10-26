#!/usr/bin/env bash
# Puts the browser build into dist/ and records where it came from. The
# website's "slicer import" workflow (Karazas-website,
# .github/workflows/slicer-import.yml) picks dist/ up from main within the
# hour, minifies the js, slices a few models with the old and new build and
# commits it to dev. Commit and push dist/ after this; nothing else needed.
# Run link_web.sh first, and the gate (eval/gate.cjs on the rewrite branch).
# Usage: publish.sh [path to kz-slicer.js]   (default $OUT/out/web/kz-slicer.js)
W=${ORCA_WASM:-$HOME/orca-wasm}; HERE=$(cd "$(dirname "$0")" && pwd); OUT=${OUT:-$W/build}
js=${1:-$OUT/out/web/kz-slicer.js}; dir=$(dirname "$js"); repo=$(dirname "$HERE")
[ -f "$js" ] && [ -f "$dir/kz-slicer.wasm" ] || { echo "no build at $dir"; exit 1; }
mkdir -p "$repo/dist"
cp "$js" "$dir/kz-slicer.wasm" "$repo/dist/"
{
  echo "built:  $(date -u +%Y-%m-%dT%H:%MZ)"
  echo "source: $(git -C "$repo" rev-parse --short HEAD) + patches/orca-2.4.2-wasm.patch $(sha256sum < "$repo/patches/orca-2.4.2-wasm.patch" | cut -c1-12)"
  echo "wasm:   $(sha256sum < "$repo/dist/kz-slicer.wasm" | cut -c1-12), $(wc -c < "$repo/dist/kz-slicer.wasm") bytes"
} > "$repo/dist/BUILD"
cat "$repo/dist/BUILD"
