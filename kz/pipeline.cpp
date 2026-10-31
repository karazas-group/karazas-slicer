// Stage: pipeline. Runs the Orca steps in order for one slice and reads the
// answer off the result. Owns the profile cache between slices.
// Calls: load_stl (Format/STL.cpp), Print::apply, Print::process,
// kz_extruded_volume (volume_sum.cpp). With -DKZ_GCODE also export_gcode, to
// cross-check the sum against Orca's own G-code totals (link_check.sh).
#include "kz.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#ifdef KZ_GCODE
#include "libslic3r/GCode/GCodeProcessor.hpp"
#endif
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <string>
#include <vector>
#include "libslic3r/BoundingBox.hpp"

using namespace Slic3r;


// The full default config plus the flattened profiles, kept between slices.
// Building it is the same for every slice with the same printer and filament.
static std::string        g_profiles_key;
static DynamicPrintConfig g_profiles_config;

static int load_profiles(const std::string &paths)
{
    if (paths == g_profiles_key) return 0;
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    // paths: "machine.json;process.json;filament.json" (already-flattened Orca profiles)
    size_t pos = 0;
    while (pos <= paths.size()) {
        size_t e = paths.find(';', pos); if (e == std::string::npos) e = paths.size();
        std::string f = paths.substr(pos, e - pos); pos = e + 1;
        if (f.empty()) continue;
        DynamicPrintConfig part; std::map<std::string, std::string> kv; std::string reason;
        ConfigSubstitutionContext subs(ForwardCompatibilitySubstitutionRule::Enable);
        if (part.load_from_json(f, subs, true, kv, reason) != 0) { std::fprintf(stderr, "load %s: %s\n", f.c_str(), reason.c_str()); return -3; }
        config.apply(part, true);
    }
    g_profiles_config = std::move(config);
    g_profiles_key = paths;
    return 0;
}

int kz::slice_one(const char *stl_path, const char *flat_config_path, const kz::Overrides &overrides, const char *gcode_out,
                      double *out_time_s, double *out_filament_mm, double *out_filament_g)
{
    try {
        kz::lap("start");
        Model model;
        if (!load_stl(stl_path, &model)) return -1;
        for (ModelObject *o : model.objects) {
            if (o->instances.empty()) o->add_instance();
            o->ensure_on_bed(); // a pricer upload can sit anywhere in Z
        }

        kz::lap("load_stl");
        if (int rc = load_profiles(flat_config_path)) return rc;
        DynamicPrintConfig config = g_profiles_config;
        for (const auto &[key, value] : overrides) {
            ConfigSubstitutionContext subs(ForwardCompatibilitySubstitutionRule::Enable);
            config.set_deserialize_nothrow(key, value, subs);
        }
        // Centre the model on the bed (printable_area may be overridden, e.g. a
        // huge bed so the pricer never refuses an oversized model).
        BoundingBoxf bed;
        if (auto *area = config.option<ConfigOptionPoints>("printable_area"))
            for (const Vec2d &p : area->values) bed.merge(p);
        model.center_instances_around_point(bed.defined ? bed.center() : Vec2d(128, 128));
        kz::lap("profiles");
        Print print;
        // Status messages mark the steps inside process() and export.
        print.set_status_callback([](const PrintBase::SlicingStatus &st) { if (!st.text.empty()) kz::lap(("~" + st.text).c_str()); });
        // Skips the "supports look necessary" warning and the check for
        // objects colliding with each other; neither changes the toolpaths.
        print.set_no_check_flag(true);
        print.set_plate_origin(Vec3d::Zero()); // Print::m_origin is an uninitialised Eigen Vec3d (Print.hpp:1192); GUI/CLI set it via PartPlate
        print.apply(model, config);
        StringObjectException err = print.validate();
        if (!err.string.empty()) { std::fprintf(stderr, "validate: %s\n", err.string.c_str()); }
        kz::lap("apply_validate");
        print.process();
        kz::lap("process");
        // Every filament in the profile shares the first one's flow ratio,
        // diameter and density: the pricer prints one material at a time.
        const PrintConfig &pc = print.config();
        const double area = M_PI * sqr(0.5 * pc.filament_diameter.get_at(0));
        const double volume = kz_extruded_volume(print) * pc.filament_flow_ratio.get_at(0);
        kz::lap("sum");
        *out_filament_mm = volume / area;
        *out_filament_g  = volume * pc.filament_density.get_at(0) * 0.001;
        *out_time_s      = 0;
#ifdef KZ_GCODE
        // Validation build: also export the G-code, and report Orca's own
        // numbers alongside, so the sum can be checked against them.
        GCodeProcessorResult result;
        print.export_gcode(gcode_out, &result, nullptr);
        kz::lap("export_gcode");
        const auto &ps = print.print_statistics();
        std::fprintf(stderr, "CHECK sum_mm %.2f gcode_mm %.2f sum_g %.3f gcode_g %.3f\n", *out_filament_mm, ps.total_used_filament, *out_filament_g, ps.total_weight);
        *out_time_s = result.print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].time;
#endif
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "orca_slice failed: %s\n", e.what());
        return -2;
    }
}
