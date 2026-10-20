#!/usr/bin/env bash
# Compiles the driver and the pricing build's stand-ins into $OUT/roots/,
# which link_web.sh and link_check.sh put ahead of libslic3r_all.a.
#   driver.o        the pricer's entry point (kz_slice), no G-code
#   driver_check.o  the same with -DKZ_GCODE: also writes G-code and prints
#                   Orca's own filament numbers next to the summed ones
#   print.o         Print.cpp with -DKZ_NO_GCODE (export_gcode throws)
#   readers.o layers.o notower.o noparser.o shims.o   see each file's header
W=${ORCA_WASM:-$HOME/orca-wasm}; HERE=$(cd "$(dirname "$0")" && pwd); OUT=${OUT:-$W/build}
mkdir -p $OUT/roots
rel=$(realpath --relative-to=$W/orca/src/libslic3r $HERE/driver)
one() { # one <name> <src relative to libslic3r> [flags]
  local name=$1 src=$2; shift 2
  bash $HERE/cc.sh $src "$@" && mv $OUT/obj/$(echo $src | tr '/' '_').o $OUT/roots/$name.o
}
one driver       $rel/wasm_driver.cpp &
one shims        $rel/wasm_shims.cpp &
one readers      $rel/wasm_readers.cpp &
wait
# Both driver builds compile through the same object path, so not in one wave.
one driver_check $rel/wasm_driver.cpp -DKZ_GCODE &
one layers       $rel/wasm_layers.cpp &
one notower      $rel/wasm_notower.cpp &
one noparser     $rel/wasm_noparser.cpp &
one print        Print.cpp -DKZ_NO_GCODE &
wait
