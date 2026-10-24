#!/usr/bin/env bash
# The browser build the website's pricer loads (assets/kz/slicer/kz-slicer.{js,wasm}
# on Karazas-website). Threads need a cross-origin isolated page; the pool
# matches the TBB clamp in wasm_driver.cpp (2..4). Profiles go in through FS.
# Run roots.sh first. The objects in roots/ replace the file readers, the
# G-code writer, the prime tower and the template parser, so none of that is
# linked (6.5 MB instead of 8.5 MB). link_check.sh is the validation build.
W=${ORCA_WASM:-$HOME/orca-wasm}; HERE=$(cd "$(dirname "$0")" && pwd); OUT=${OUT:-$W/build}
source $W/emsdk/emsdk_env.sh >/dev/null 2>&1; cd $OUT; mkdir -p out/${DEST:-web}
BL=$W/deps-src/boost-1.84.0/bwasm/stage/lib; T=$W/deps-src/onetbb/build-mt2/clang_24.0_cxx11_32_release/libtbb.a; Q=$W/deps-src/qhull/bwasm2
em++ -O2 -pthread -fwasm-exceptions -sSUPPORT_LONGJMP=wasm -sMALLOC=mimalloc ${ROOTS:-roots/driver.o roots/shims.o roots/readers.o roots/layers.o roots/print.o roots/notower.o roots/noparser.o} libslic3r_all.a libdeps.a libboostlog.a $BL/libboost_filesystem.a $BL/libboost_thread.a $BL/libboost_locale.a $BL/libboost_nowide.a $BL/libboost_date_time.a $BL/libboost_chrono.a $BL/libboost_atomic.a $BL/libboost_container.a $T $Q/libqhullcpp.a $Q/libqhullstatic_r.a -Wl,--error-limit=0 -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=2GB -sINITIAL_MEMORY=256MB -sSTACK_SIZE=4MB -sDEFAULT_PTHREAD_STACK_SIZE=4MB -sPTHREAD_POOL_SIZE='Math.max(2,Math.min(8,navigator.hardwareConcurrency||2))' -sENVIRONMENT=web,worker,node -sMODULARIZE=1 -sEXPORT_NAME=KzSlicer -sINVOKE_RUN=0 -sEXIT_RUNTIME=0 -sEXPORTED_FUNCTIONS=_kz_slice,_main -sEXPORTED_RUNTIME_METHODS=ccall,FS "$@" -o $OUT/out/${DEST:-web}/kz-slicer.js
