#!/usr/bin/env bash
# Downloads everything the wasm build needs into $ORCA_WASM (default
# ~/orca-wasm) and applies our patches to Orca. Then run setup-deps.sh, and
# the build steps in README.md "Building". Re-running skips what is there.
set -e
W=${ORCA_WASM:-$HOME/orca-wasm}; REPO=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p $W/deps-src; cd $W
clone() { [ -d "$2" ] || git clone -q --depth 1 --branch "$3" "$1" "$2"; }

[ -d emsdk ] || { git clone -q https://github.com/emscripten-core/emsdk.git emsdk && (cd emsdk && ./emsdk install 6.0.13 && ./emsdk activate 6.0.13); }

if [ ! -d orca ]; then
  clone https://github.com/SoftFever/OrcaSlicer.git orca v2.4.2
  patch -p1 -s -d orca < $REPO/patches/orca-2.4.2-wasm.patch
  patch -p1 -s -d orca < $REPO/patches/clipper2.patch
  patch -p1 -s -d orca < $REPO/patches/parallel-brim.patch
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
