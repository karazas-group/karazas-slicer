#!/usr/bin/env bash
W=${ORCA_WASM:-$HOME/orca-wasm}; HERE=$(cd "$(dirname "$0")" && pwd); OUT=${OUT:-$W/build}
source $W/emsdk/emsdk_env.sh >/dev/null 2>&1; cd $OUT
BL=$W/deps-src/boost-1.84.0/bwasm/stage/lib; T=$W/deps-src/onetbb/build-mt2/clang_24.0_cxx11_32_release/libtbb.a; Q=$W/deps-src/qhull/bwasm2
em++ -O2 -pthread -fwasm-exceptions -sSUPPORT_LONGJMP=wasm -sMALLOC=mimalloc roots/driver.o roots/shims.o libslic3r_all.a libdeps.a libboostlog.a $BL/libboost_filesystem.a $BL/libboost_thread.a $BL/libboost_locale.a $BL/libboost_nowide.a $BL/libboost_date_time.a $BL/libboost_chrono.a $BL/libboost_atomic.a $BL/libboost_container.a $T $Q/libqhullcpp.a $Q/libqhullstatic_r.a -Wl,--error-limit=0 -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=4GB -sINITIAL_MEMORY=256MB -sSTACK_SIZE=4MB -sDEFAULT_PTHREAD_STACK_SIZE=4MB -sPTHREAD_POOL_SIZE=${POOL:-4} -sEXIT_RUNTIME=1 -sNODERAWFS=1 "$@" -o $OUT/out/orca_dbg.js
