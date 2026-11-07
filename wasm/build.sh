#!/usr/bin/env bash
# Builds out/web/kz-slicer.{js,wasm} from a tree set up by fetch.sh and
# setup-deps.sh: every libslic3r source (cc.sh, four at a time), deps.sh, the
# two archives, roots.sh, link_web.sh. Sources that need libraries we cut
# (OCCT, NLopt, OpenVDB, OpenCV, freetype, CGAL's exact kernel) fail to compile
# and are left out; the list of failures is printed and should be exactly
# those 12. Logs: $OUT/logs.
W=${KZ_WASM:-$HOME/kz-wasm}; HERE=$(cd "$(dirname "$0")" && pwd); OUT=${OUT:-$W/build}
source $W/emsdk/emsdk_env.sh >/dev/null 2>&1
mkdir -p $OUT; rm -f $OUT/obj/*.o
{ grep -v -E '^(clipper\.cpp|Clipper2Utils\.cpp)$' $HERE/libslic3r-sources.txt; echo ClipperZUtils.cpp; } | sort -u |
{
  while read -r f; do
    [ -n "$f" ] || continue
    bash $HERE/cc.sh "$f" &
    while [ $(jobs -r | wc -l) -ge ${JOBS:-4} ]; do wait -n; done
  done
  wait   # inside the pipe's subshell, where the jobs are
} > $OUT/cc.txt
echo "compiled: $(grep -c 'rc=0' $OUT/cc.txt), failed: $(grep -vc 'rc=0' $OUT/cc.txt)"
grep -v 'rc=0' $OUT/cc.txt | cut -d' ' -f1
bash $HERE/deps.sh
cd $OUT && rm -f libslic3r_all.a libdeps.a
emar rcs libslic3r_all.a $(ls obj/*.o | grep -v '/roots_')
emar rcs libdeps.a depobj/*.o
bash $HERE/roots.sh
bash $HERE/link_web.sh && ls -la $OUT/out/web/
