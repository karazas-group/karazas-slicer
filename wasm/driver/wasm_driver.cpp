// Minimal headless driver: STL + flat config (key=value lines) -> slice -> G-code -> stats.
// Used (a) as the root for object-level reachability analysis, (b) as the future embind entry point.
#include "libslic3r/libslic3r.h"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include <tbb/global_control.h>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <string>

using namespace Slic3r;
static int g_argc = 0; static char **g_argv = nullptr;

extern "C" int orca_slice(const char *stl_path, const char *flat_config_path, const char *gcode_out,
                          double *out_time_s, double *out_filament_mm, double *out_filament_g)
{
    try {
        Model model;
        if (!load_stl(stl_path, &model)) return -1;
        for (ModelObject *o : model.objects) if (o->instances.empty()) o->add_instance();
        model.center_instances_around_point(Vec2d(128, 128));

        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        // flat_config_path: "machine.json;process.json;filament.json" (already-flattened Orca profiles)
        std::string paths = flat_config_path;
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
        for (int i = 4; i + 1 < g_argc; i += 2) { // optional key value overrides
            ConfigSubstitutionContext subs(ForwardCompatibilitySubstitutionRule::Enable);
            config.set_deserialize_nothrow(g_argv[i], g_argv[i + 1], subs);
        }

        Print print;
        print.set_plate_origin(Vec3d::Zero()); // Print::m_origin is an uninitialised Eigen Vec3d (Print.hpp:1192); GUI/CLI set it via PartPlate
        print.apply(model, config);
        StringObjectException err = print.validate();
        if (!err.string.empty()) { std::fprintf(stderr, "validate: %s\n", err.string.c_str()); }
        print.process();
        GCodeProcessorResult result;
        print.export_gcode(gcode_out, &result, nullptr);

        const auto &ps = print.print_statistics();
        *out_time_s      = result.print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].time;
        *out_filament_mm = ps.total_used_filament;
        *out_filament_g  = ps.total_weight;
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "orca_slice failed: %s\n", e.what());
        return -2;
    }
}

int main(int argc, char **argv)
{
    if (argc < 4) return 1;
    g_argc = argc; g_argv = argv;
    // oneTBB on emscripten defaults worker stacks to __TBB_EMSCRIPTEN_STACK_SIZE = 64 KB (onetbb cmake/compilers/Clang.cmake:21),
    // which Arachne's recursive SkeletalTrapezoidation::propagateBeadingsDownward overflows silently -> heap corruption.
    tbb::global_control stack_ctl(tbb::global_control::thread_stack_size, 4u << 20);
    double t = 0, mm = 0, g = 0;
    int rc = orca_slice(argv[1], argv[2], argv[3], &t, &mm, &g);
    std::printf("rc=%d time_s=%.1f filament_mm=%.1f filament_g=%.2f\n", rc, t, mm, g);
    return rc;
}
