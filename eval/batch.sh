#!/bin/sh
# Orca reference runs. A = the pricer's own settings on the P2S machine, B/C = stock P2S / Ender-3 S1 profiles.
EQ="wall_loops=2 sparse_infill_density=15% sparse_infill_pattern=grid top_shell_layers=4 bottom_shell_layers=4 top_shell_thickness=0 bottom_shell_thickness=0 skirt_loops=0 brim_type=no_brim line_width=0.45 outer_wall_line_width=0.45 inner_wall_line_width=0.45 sparse_infill_line_width=0.45 internal_solid_infill_line_width=0.45 top_surface_line_width=0.45 initial_layer_line_width=0.45"
ORG="enable_support=1 support_type=tree(auto) support_style=organic support_threshold_angle=35"
for m in cube20 cup sphere40 invcone mushroom bracket benchy; do
  M=models/$m.stl
  echo "$m eq-s0 $(python3 run.py p2s $M $m-eq0 $EQ)"
  echo "$m eq-n45 $(python3 run.py p2s $M $m-eq1 $EQ enable_support=1 'support_type=normal(auto)' support_style=default support_threshold_angle=45)"
  echo "$m eq-org35 $(python3 run.py p2s $M $m-eq2 $EQ $ORG)"
  echo "$m p2s-s0 $(python3 run.py p2s $M $m-p0)"
  echo "$m p2s-org35 $(python3 run.py p2s $M $m-p1 $ORG)"
  echo "$m s1-s0 $(python3 run.py s1 $M $m-c0)"
  echo "$m s1-org35 $(python3 run.py s1 $M $m-c1 $ORG)"
done
