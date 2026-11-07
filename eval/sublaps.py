# python3 eval/sublaps.py <orca>/src/libslic3r/PrintObject.cpp: adds kz_lap() sub-laps inside prepare_infill
# and bridge_over_infill for a KZ_IDLE experiment build. Edit a copy or restore the file after.
import sys
p=sys.argv[1]; s=open(p).read()
s=s.replace('void PrintObject::prepare_infill()','extern "C" void kz_lap(const char *);\nvoid PrintObject::prepare_infill()',1)
a=s.index('void PrintObject::prepare_infill()'); b=s.index('void PrintObject::infill()')
body=s[a:b]
for call,name in [('    this->discover_vertical_shells();\n','pi:vertical_shells'),('    this->discover_horizontal_shells();\n','pi:horizontal_shells'),
  ('    this->process_external_surfaces();\n','pi:external_surfaces'),('    this->clip_fill_surfaces();\n','pi:clip_fill_surfaces'),
  ('    this->bridge_over_infill();\n','pi:bridge_gather'),('    this->combine_infill();\n','pi:combine_infill')]:
    assert body.count(call)==1, call; body=body.replace(call,'    kz_lap("%s");\n'%name+call)
s=s[:a]+body+s[b:]
def lap(anchor,name,after=False):
    global s
    assert s.count(anchor)==1,anchor
    s=s.replace(anchor,(anchor+'\nkz_lap("%s");\n' if after else 'kz_lap("%s");\n'+anchor)%name,1)
lap('        for (const CandidateSurface &c : candidate_surfaces) {\n            surfaces_by_layer[c.layer_index].push_back(c);','bo:group')
lap('    std::map<size_t, Polylines> infill_lines;','bo:infill_lines')
lap('    // cluster layers by depth needed for thick bridges.','bo:cluster')
lap('    tbb::parallel_for(tbb::blocked_range<size_t>(0, clustered_layers_for_threads.size())','bo:clusters_run')
lap('} // void PrintObject::bridge_over_infill()','pi:after_bridge')
open(p,'w').write(s)
