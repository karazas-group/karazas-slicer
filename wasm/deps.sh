#!/usr/bin/env bash
W=${ORCA_WASM:-$HOME/orca-wasm}; HERE=$(cd "$(dirname "$0")" && pwd); OUT=${OUT:-$W/build}
source $W/emsdk/emsdk_env.sh >/dev/null 2>&1
D=$W/orca/deps_src; OUT=$OUT/depobj; mkdir -p $OUT
CXXF="-std=c++17 -O2 -pthread -fwasm-exceptions -DNDEBUG"
CF="-O2 -pthread -DNDEBUG -sSUPPORT_LONGJMP=wasm"
ok=0; bad=0
for f in $D/admesh/*.cpp; do em++ $CXXF -isystem $W/deps-src/eigen-5.0.1 -isystem $W/prefix-mt/include -I$HERE/stubs -I$HERE/gen -I$W/orca/src -I$W/orca/src/libslic3r -isystem $W/deps-src/cereal/include -DSLIC3R_NO_STEP -I$D -c $f -o $OUT/admesh_$(basename $f).o && ok=$((ok+1)) || bad=$((bad+1)); done
# Clipper2 (the PR 15969 port): the engine includes <oneapi/tbb/scalable_allocator.h>, hence prefix-mt
for f in $D/clipper2/Clipper2Lib/src/*.cpp; do em++ $CXXF -isystem $W/prefix-mt/include -I$D/clipper2/Clipper2Lib/include -c $f -o $OUT/c2_$(basename $f).o && ok=$((ok+1)) || bad=$((bad+1)); done
emcc $CF -c $D/semver/semver.c -o $OUT/semver.o && ok=$((ok+1)) || bad=$((bad+1))
emcc $CF -c $D/miniz/miniz.c -o $OUT/miniz.o && ok=$((ok+1)) || bad=$((bad+1))
for f in $(for n in dict geom memalloc mesh normal priorityq render sweep tess tessmono; do echo $D/glu-libtess/src/$n.c; done); do emcc $CF -I$D/glu-libtess/include -c $f -o $OUT/tess_$(basename $f).o && ok=$((ok+1)) || bad=$((bad+1)); done
echo ok=$ok bad=$bad
