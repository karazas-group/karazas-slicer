#!/usr/bin/env bash
# Downloads everything the wasm build needs into $KZ_WASM (default
# ~/kz-wasm) and applies our patches to the upstream source. Then run setup-deps.sh, and
# the build steps in README.md "Building". Re-running skips what is there.
set -e
W=${KZ_WASM:-$HOME/kz-wasm}; REPO=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p $W/deps-src; cd $W
clone() { [ -d "$2" ] || git clone -q --depth 1 --branch "$3" "$1" "$2"; }

[ -d emsdk ] || { git clone -q https://github.com/emscripten-core/emsdk.git emsdk && (cd emsdk && ./emsdk install 6.0.13 && ./emsdk activate 6.0.13); }

if [ ! -d upstream ]; then
  clone https://github.com/SoftFever/OrcaSlicer.git upstream v2.4.2
  patch -p1 -s -d upstream < $REPO/patches/libslic3r-2.4.2.patch
  patch -p1 -s -d upstream < $REPO/patches/clipper2.patch
  patch -p1 -s -d upstream < $REPO/patches/parallel-stl-load.patch
fi

cd deps-src
[ -d boost-1.84.0 ] || { curl -sSL https://archives.boost.io/release/1.84.0/source/boost_1_84_0.tar.gz | tar xz && mv boost_1_84_0 boost-1.84.0; }
[ -d CGAL-5.6.3 ] || curl -sSL https://github.com/CGAL/cgal/releases/download/v5.6.3/CGAL-5.6.3.tar.xz | tar xJ
[ -d onetbb ] || { git clone -q https://github.com/uxlfoundation/oneTBB.git onetbb && git -C onetbb checkout -q af14bb5; }
clone https://github.com/qhull/qhull.git qhull v8.0.2
clone https://gitlab.com/libeigen/eigen.git eigen-5.0.1 5.0.1
clone https://github.com/USCiLab/cereal.git cereal v1.3.2
clone https://github.com/SoftFever/Orca-deps-libnoise.git libnoise 1.0
clone https://github.com/glennrp/libpng.git libpng v1.6.35
clone https://github.com/madler/zlib.git zlib v1.3.1
echo "fetched into $W"
