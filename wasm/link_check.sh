#!/usr/bin/env bash
# Validation build: the full G-code export linked back in (driver_check.o, and
# the archive's own Print.cpp and G-code code). Each slice prints
#   CHECK sum_mm .. gcode_mm .. sum_g .. gcode_g ..
# to stderr, comparing the summed toolpaths with Orca's G-code totals.
HERE=$(cd "$(dirname "$0")" && pwd)
ROOTS="roots/driver_check.o roots/shims.o roots/readers.o" DEST=check exec bash $HERE/link_web.sh "$@"
