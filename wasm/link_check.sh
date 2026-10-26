#!/usr/bin/env bash
# Validation build: the full G-code export linked back in (pipeline_check.o, and
# the archive's own Print.cpp and G-code code). Each slice prints
#   CHECK sum_mm .. gcode_mm .. sum_g .. gcode_g ..
# to stderr, comparing the summed toolpaths with Orca's G-code totals.
HERE=$(cd "$(dirname "$0")" && pwd)
ROOTS="$(awk '$1!~/^#/&&($2=="check"||$2=="both"){printf "roots/%s.o ",$1}' $HERE/../kz/sources.txt)" DEST=check exec bash $HERE/link_web.sh "$@"
