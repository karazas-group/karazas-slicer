#!/bin/bash
# wasm parity matrix: 7 models x {p2s,s1} x {no supports, organic 35}
HERE=$(cd "$(dirname "$0")" && pwd); P=$HERE/profiles; M=$HERE/models
S=${S:-/tmp}  # w.gcode goes here; benchy.stl is read from $S/extra/ if it isn't in models/
ORG="enable_support 1 support_type tree(auto) support_style organic support_threshold_angle 35"
JS=${JS:-${ORCA_WASM:-$HOME/orca-wasm}/build/out/orca_fixed.js}
for m in cube20 cup sphere40 invcone mushroom bracket benchy; do
  f=$M/$m.stl; [ -f $f ] || f=$S/extra/$m.stl
  for pr in p2s s1; do for sup in s0 org35; do
    extra=""; [ $sup = org35 ] && extra=$ORG
    t0=$(date +%s.%N)
    r=$(node $JS $f "$P/$pr-machine.json;$P/$pr-process.json;$P/$pr-pla.json" $S/w.gcode $extra 2>&1 | grep -E "^(rc=|\{\"rc\")|RuntimeError" | head -1)
    echo "$m $pr-$sup $r wall=$(printf %.1f $(echo "$(date +%s.%N)-$t0" | bc))s"
  done; done
done
