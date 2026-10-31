#!/usr/bin/env bash
# Compiles everything listed in kz/sources.txt into $OUT/roots/<name>.o, which
# link_web.sh and link_check.sh put ahead of libslic3r_all.a. Stage files are
# described in REWRITE.md; stand-ins in the header of each kz/stubs file.
# Usage: roots.sh [name ...]   (no names: all)
W=${ORCA_WASM:-$HOME/orca-wasm}; HERE=$(cd "$(dirname "$0")" && pwd); OUT=${OUT:-$W/build}
REPO=$(dirname "$HERE")
mkdir -p $OUT/roots
want=" $* "
while read -r name link src flags; do
  case "$name" in ''|'#'*) continue;; esac
  [ "$want" = "  " ] || case "$want" in *" $name "*) ;; *) continue;; esac
  case "$src" in
    orca:*) rel=${src#orca:} ;;
    *)      rel=$(realpath --relative-to=$W/orca/src/libslic3r $REPO/$src) ;;
  esac
  # one cc.sh per object, four at a time; OBJ keeps two builds of one source apart
  ( OBJ=roots_$name bash $HERE/cc.sh $rel $flags && mv $OUT/obj/roots_$name.o $OUT/roots/$name.o ) &
  while [ $(jobs -r | wc -l) -ge 4 ]; do wait -n; done
done < $REPO/kz/sources.txt
wait
