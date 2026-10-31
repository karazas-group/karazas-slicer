// Stage: extrusion volume sum.
// Replaces: GCode::_extrude's flow maths plus GCodeProcessor's filament total
// (GCode.cpp, GCode/GCodeProcessor.cpp), which the pricing build never links.
// In:  a Print after process() (layers, support layers, skirt, brim).
// Out: mm^3 of filament before filament_flow_ratio (the caller applies that).
// Exactness: the sum runs object by object, layer by layer, region by region,
// perimeters before fills, then support, skirt, brim in map order. Changing
// that order changes the last bits of the double and so can move the grams.
#include "kz.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"

using namespace Slic3r;

// Filament the print lays down, in mm^3, summed from the toolpaths that
// Print::process() leaves behind, with the same flow multipliers G-code
// export applies (GCode::_extrude). Writing and re-parsing the G-code would
// give the same total plus the start G-code purge, at 30-75% of the slice
// time and about a megabyte of code the pricer never needs.
struct FlowRatios {
    const PrintObjectConfig &oc;
    const PrintRegionConfig *rc; // support, skirt and brim use the object's first region
    bool first_layer = false;
};

static double path_volume(const ExtrusionPath &path, const FlowRatios &f)
{
    const PrintRegionConfig &c = *f.rc;
    const ExtrusionRole role = path.role();
    double k = c.print_flow_ratio.value;
    if (role == erTopSolidInfill) k *= c.top_solid_infill_flow_ratio.value;
    else if (role == erBottomSurface) k *= c.bottom_solid_infill_flow_ratio.value;
    else if (role == erInternalBridgeInfill) k *= c.internal_bridge_flow.value;
    else if (role == erBrim) k *= f.oc.brim_flow_ratio.value;
    if (f.oc.set_other_flow_ratios.value) {
        switch (role) {
        case erExternalPerimeter:        k *= c.outer_wall_flow_ratio.value; break;
        case erPerimeter:                k *= c.inner_wall_flow_ratio.value; break;
        case erOverhangPerimeter:        k *= c.overhang_flow_ratio.value; break;
        case erInternalInfill:           k *= c.sparse_infill_flow_ratio.value; break;
        case erSolidInfill:              k *= c.internal_solid_infill_flow_ratio.value; break;
        case erGapFill:                  k *= c.gap_fill_flow_ratio.value; break;
        case erSupportMaterial:          k *= f.oc.support_flow_ratio.value; break;
        case erSupportMaterialInterface: k *= f.oc.support_interface_flow_ratio.value; break;
        default: break;
        }
        if (f.first_layer && role != erBrim && role != erSkirt) k *= c.first_layer_flow_ratio.value;
    }
    return path.mm3_per_mm * unscale<double>(path.length()) * k;
}

static double entity_volume(const ExtrusionEntity *e, const FlowRatios &f)
{
    if (auto *c = dynamic_cast<const ExtrusionEntityCollection *>(e)) {
        double v = 0;
        for (const ExtrusionEntity *x : c->entities) v += entity_volume(x, f);
        return v;
    }
    if (auto *l = dynamic_cast<const ExtrusionLoop *>(e)) {
        double v = 0;
        for (const ExtrusionPath &p : l->paths) v += path_volume(p, f);
        return v;
    }
    if (auto *m = dynamic_cast<const ExtrusionMultiPath *>(e)) {
        double v = 0;
        for (const ExtrusionPath &p : m->paths) v += path_volume(p, f);
        return v;
    }
    if (auto *p = dynamic_cast<const ExtrusionPath *>(e)) return path_volume(*p, f);
    return 0;
}

double kz_extruded_volume(Print &print)
{
    double total = 0;
    for (const PrintObject *obj : print.objects()) {
        if (!obj->num_printing_regions()) continue;
        double v = 0;
        FlowRatios f{obj->config(), &obj->printing_region(0).config()};
        for (const Layer *layer : obj->layers()) {
            f.first_layer = layer->id() == 0;
            for (const LayerRegion *layerm : layer->regions()) {
                f.rc = &layerm->region().config();
                v += entity_volume(&layerm->perimeters, f) + entity_volume(&layerm->fills, f);
            }
        }
        f.rc = &obj->printing_region(0).config();
        for (const SupportLayer *layer : obj->support_layers()) {
            f.first_layer = layer->id() == 0;
            v += entity_volume(&layer->support_fills, f);
        }
        total += v * obj->instances().size();
    }
    if (!print.objects().empty() && print.objects().front()->num_printing_regions()) {
        const PrintObject *obj = print.objects().front();
        FlowRatios f{obj->config(), &obj->printing_region(0).config()};
        total += entity_volume(&print.skirt(), f);
        for (auto &[id, brim] : print.get_brimMap()) total += entity_volume(&brim, f);
    }
    return total;
}
