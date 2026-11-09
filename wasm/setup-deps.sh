#!/usr/bin/env bash
# Builds the wasm dependencies under $KZ_WASM (default ~/kz-wasm), the
# layout README.md "Building" describes. Sources must already be there:
#   emsdk/ (Emscripten 6.0.13, activated), upstream/ (v2.4.2 + patches),
#   deps-src/{boost-1.84.0, onetbb @af14bb5, qhull @v8.0.2, eigen-5.0.1,
#             cereal @v1.3.2, CGAL-5.6.3, libnoise (SoftFever/Orca-deps-libnoise
#             @1.0), libpng @v1.6.35 and zlib @v1.3.1 (headers only)}
# fetch.sh downloads all of them.
# Flags are the ones REPORT.md §1 records: TBB with -fwasm-exceptions (same EH
# model as libslic3r), qhull with -sSUPPORT_LONGJMP=wasm, Boost without
# context/coroutine/fiber. Steps can be run alone: setup-deps.sh tbb boost ...
set -e
W=${KZ_WASM:-$HOME/kz-wasm}; S=$W/deps-src; J=${JOBS:-$(nproc)}; REPO=$(cd "$(dirname "$0")/.." && pwd)
source $W/emsdk/emsdk_env.sh >/dev/null 2>&1
steps=${*:-tbb qhull boost boostlog headers libnoise}

tbb() {
  emcmake cmake -S $S/onetbb -B $S/onetbb/build-mt2 -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DTBB_STRICT=OFF -DTBBMALLOC_BUILD=OFF -DTBB_TEST=OFF -DTBB_EXAMPLES=OFF -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_CXX_FLAGS="-pthread -fwasm-exceptions" -DCMAKE_C_FLAGS="-pthread" >/dev/null
  ninja -C $S/onetbb/build-mt2 -j$J tbb
  # link_*.sh name the archive by this directory; keep the name stable
  local lib=$(find $S/onetbb/build-mt2 -name libtbb.a | head -1)
  mkdir -p $S/onetbb/build-mt2/clang_24.0_cxx11_32_release
  [ "$lib" = "$S/onetbb/build-mt2/clang_24.0_cxx11_32_release/libtbb.a" ] || ln -sf $lib $S/onetbb/build-mt2/clang_24.0_cxx11_32_release/libtbb.a
}

qhull() {
  emcmake cmake -S $S/qhull -B $S/qhull/bwasm2 -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_C_FLAGS="-pthread -sSUPPORT_LONGJMP=wasm" -DCMAKE_CXX_FLAGS="-pthread -fwasm-exceptions -sSUPPORT_LONGJMP=wasm" >/dev/null
  ninja -C $S/qhull/bwasm2 -j$J qhullcpp qhullstatic_r
}

boost() {
  cd $S/boost-1.84.0
  [ -x ./b2 ] || ./bootstrap.sh >/dev/null
  # b2's own emscripten toolset archives .bc files that emcc 6 writes as wasm
  # and then can't read back; clang pointed at em++/emar works.
  echo "using clang : wasm : em++ : <archiver>emar <ranlib>emranlib ;" > bwasm-user-config.jam
  ./b2 -j$J --user-config=bwasm-user-config.jam toolset=clang-wasm target-os=linux \
    link=static threading=multi variant=release runtime-link=static \
    cxxflags="-std=c++17 -pthread -fwasm-exceptions" cflags="-pthread" \
    define=BOOST_ALL_NO_LIB \
    --build-dir=bwasm/build --stagedir=bwasm/stage \
    --with-filesystem --with-thread --with-locale --with-nowide --with-date_time \
    --with-chrono --with-atomic --with-container \
    boost.locale.icu=off boost.locale.posix=on stage
}

# Boost.Log by hand (REPORT.md §1): its b2 build wants more than wasm has.
boostlog() {
  local B=$S/boost-1.84.0 O=$S/boost-1.84.0/bwasm/log; mkdir -p $O
  local F="-std=c++17 -O2 -pthread -fwasm-exceptions -DNDEBUG -DBOOST_ALL_NO_LIB -DBOOST_LOG_BUILDING_THE_LIB \
    -DBOOST_LOG_WITHOUT_SYSLOG -DBOOST_LOG_WITHOUT_IPC -DBOOST_LOG_WITHOUT_EVENT_LOG -DBOOST_LOG_WITHOUT_DEBUG_OUTPUT \
    -DBOOST_LOG_USE_STD_REGEX -DBOOST_LOG_NO_ASIO -DBOOST_LOG_WITHOUT_SETTINGS_PARSERS -I$B -I$B/libs/log/src"
  local n=0
  for f in $B/libs/log/src/*.cpp; do
    case $(basename $f) in dump_avx2.cpp|dump_ssse3.cpp|event_log_backend.cpp|syslog_backend.cpp|debug_output_backend.cpp|named_scope_format_parser.cpp) continue;; esac
    em++ $F -c $f -o $O/$(basename $f .cpp).o 2>$O/$(basename $f .cpp).log &
    n=$((n+1)); [ $((n % J)) -eq 0 ] && wait
  done; wait
  local out=${OUT:-$W/build}; mkdir -p $out
  rm -f $out/libboostlog.a; emar rcs $out/libboostlog.a $O/*.o
  echo "boost.log: $(ls $O/*.o | wc -l) objects"
}

# prefix-mt/include: the headers cc.sh and deps.sh look for in one place
headers() {
  local P=$W/prefix-mt/include; mkdir -p $P
  cp -r $S/boost-1.84.0/boost $P/ 2>/dev/null || true
  # Faster big-int multiply for the exact Voronoi fallback (same results)
  patch -p1 -s -d $P < $REPO/patches/boost-voronoi-mul.patch
  cp -r $S/onetbb/include/* $P/
  mkdir -p $P/libqhull_r $P/libqhullcpp
  cp $S/qhull/src/libqhull_r/*.h $P/libqhull_r/; cp $S/qhull/src/libqhullcpp/*.h $P/libqhullcpp/
  # EdgeGrid.cpp and PNGReadWrite.cpp include png.h; their PNG paths are dead
  # in the pricer, so headers are enough (the symbols stay unresolved, unused)
  cp $S/libpng/png.h $S/libpng/pngconf.h $P/; cp $S/libpng/scripts/pnglibconf.h.prebuilt $P/pnglibconf.h
  cp $S/zlib/zlib.h $S/zlib/zconf.h $P/
  mkdir -p $P/libnoise; (cd $S/libnoise/src && find . -name '*.h' | cpio -pdm --quiet $P/libnoise)
}

# FuzzySkin.cpp includes libnoise; PerimeterGenerator references it even with
# fuzzy skin off. Objects go to $OUT/depobj/noise_*.o for libdeps.a.
libnoise() {
  local O=${OUT:-$W/build}/depobj; mkdir -p $O; local n=0
  for f in $(find $S/libnoise/src -name '*.cpp'); do
    em++ -std=c++17 -O2 -pthread -fwasm-exceptions -DNDEBUG -I$S/libnoise/src -c $f -o $O/noise_$(basename $f .cpp).o &
    n=$((n+1)); [ $((n % J)) -eq 0 ] && wait
  done; wait
  echo "libnoise: $(ls $O/noise_*.o | wc -l) objects"
}

for s in $steps; do echo "== $s"; $s; done
