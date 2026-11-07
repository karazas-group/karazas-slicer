// Experiment (not linked in the product): the exact volume per extrusion role
// next to area x height estimates from the surfaces process() leaves, to see
// how close grams get without toolpaths. Prints "R {json}" to stderr.
#include "kz.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include <cstdio>
#include <map>
#include <string>
using namespace Slic3r;

static std::map<std::string, double> g_exact, g_est;
static void walk(const ExtrusionEntity *e) {
    if (auto *c = dynamic_cast<const ExtrusionEntityCollection *>(e)) { for (auto *x : c->entities) walk(x); return; }
    auto add = [](const ExtrusionPath &p) { g_exact[ExtrusionEntity::role_to_string(p.role())] += p.mm3_per_mm * unscale<double>(p.length()); };
    if (auto *l = dynamic_cast<const ExtrusionLoop *>(e)) { for (auto &p : l->paths) add(p); return; }
    if (auto *m = dynamic_cast<const ExtrusionMultiPath *>(e)) { for (auto &p : m->paths) add(p); return; }
    if (auto *p = dynamic_cast<const ExtrusionPath *>(e)) add(*p);
}
static double area_mm2(const ExPolygons &v) { double a = 0; for (auto &x : v) a += x.area(); return a * SCALING_FACTOR * SCALING_FACTOR; }

namespace kz { void kz_roles_probe(Print &print);}
void kz::kz_roles_probe(Print &print)
{
    g_exact.clear(); g_est.clear();
    for (const PrintObject *obj : print.objects()) {
        for (const Layer *layer : obj->layers()) {
            double h = layer->height;
            g_est["slice"] += area_mm2(layer->lslices) * h;
            for (const LayerRegion *lm : layer->regions()) {
                walk(&lm->perimeters); walk(&lm->fills);
                double d = lm->region().config().sparse_infill_density.value / 100.;
                double s = 0, f = 0;
                for (auto &x : lm->slices.surfaces) s += x.expolygon.area();
                s *= SCALING_FACTOR * SCALING_FACTOR;
                for (auto &x : lm->fill_surfaces.surfaces) {
                    double a = x.expolygon.area() * SCALING_FACTOR * SCALING_FACTOR; f += a;
                    std::string k = x.surface_type == stInternal ? "e_sparse" : x.surface_type == stInternalSolid ? "e_solid" :
                        x.surface_type == stTop ? "e_top" : x.surface_type == stBottom ? "e_bottom" : x.surface_type == stBottomBridge ? "e_bridge" :
                        x.surface_type == stInternalBridge ? "e_ibridge" : x.surface_type == stInternalVoid ? "e_void" : "e_other";
                    g_est[k] += a * h; if (k == "e_sparse") g_est["e_sparse_d"] += a * h * d;
                }
                g_est["e_ring"] += (s - f) * h;
                g_est["e_fillexp"] += area_mm2(lm->fill_expolygons) * h;
            }
        }
        for (const SupportLayer *sl : obj->support_layers()) {
            walk(&sl->support_fills);
            g_est["e_sup_islands"] += area_mm2(sl->support_islands) * sl->height;
            g_est["e_sup_base"] += area_mm2(sl->base_areas) * sl->height;
        }
    }
    walk(&print.skirt()); for (auto &[id, b] : print.get_brimMap()) walk(&b);
    std::string o = "R {";
    for (auto *m : {&g_exact, &g_est}) for (auto &[k, v] : *m) { char b[96]; std::snprintf(b, sizeof b, "\"%s\":%.3f,", k.c_str(), v); o += b; }
    o.back() = '}'; std::fprintf(stderr, "%s\n", o.c_str());
}
