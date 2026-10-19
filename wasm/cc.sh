#!/usr/bin/env bash
W=${ORCA_WASM:-$HOME/orca-wasm}; HERE=$(cd "$(dirname "$0")" && pwd); OUT=${OUT:-$W/build}
# usage: cc.sh <src relative to libslic3r> [extra flags]
source $W/emsdk/emsdk_env.sh >/dev/null 2>&1
O=$W/orca; D=$O/deps_src; P=$W/prefix-mt; S=$W/deps-src
src=$1; shift
out=$OUT/obj/$(echo $src | tr '/' '_').o
mkdir -p $OUT/obj $OUT/logs
log=$OUT/logs/$(echo $src | tr '/' '_').log
start=$(date +%s)
em++ -std=c++17 -O2 -pthread -fwasm-exceptions -DNDEBUG -DUSE_TBB -DTBB_USE_CAPTURED_EXCEPTION=0 -DBOOST_ALL_NO_LIB -DSLIC3R_NO_STEP -DSLIC3R_NO_JPEG \
  -DBBL_RELEASE_TO_PUBLIC=1 -DGIT_COMMIT_HASH='"8500fcd"' \
  -I$HERE/stubs -I$HERE/gen -I$O/src/libslic3r -I$O/src \
  -isystem $D -isystem $D/libnest2d/include -isystem $D/clipper2/Clipper2Lib/include -isystem $D/libigl -isystem $D/semver -isystem $D/miniz \
  -isystem $P/include -isystem $S/eigen-5.0.1 -isystem $S/cereal/include -isystem $S/CGAL-5.6.3/include -isystem $D/expat -isystem $D/glu-libtess/include -DCGAL_DISABLE_GMP=1 -DCGAL_DISABLE_ROUNDING_MATH_CHECK \
  "$@" -c $O/src/libslic3r/$src -o $out > $log 2>&1
rc=$?
echo "$src rc=$rc t=$(( $(date +%s)-start ))s errors=$(grep -c 'error:' $log) warnings=$(grep -c 'warning:' $log)"
