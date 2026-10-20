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
#include <chrono>
#include <vector>
#include <emscripten.h>
#include <emscripten/threading.h>
#include "libslic3r/BoundingBox.hpp"

using namespace Slic3r;
// Phase timer: prints "T <phase> <ms>" to stderr so a run can be split into steps.
static auto g_t0 = std::chrono::steady_clock::now();
static void lap(const char *phase)
{
    auto now = std::chrono::steady_clock::now();
    std::fprintf(stderr, "T %s %.0f\n", phase, std::chrono::duration<double, std::milli>(now - g_t0).count());
    g_t0 = now;
}
using Overrides = std::vector<std::pair<std::string, std::string>>;

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

static int orca_slice(const char *stl_path, const char *flat_config_path, const Overrides &overrides, const char *gcode_out,
                      double *out_time_s, double *out_filament_mm, double *out_filament_g)
{
    try {
        lap("start");
        Model model;
        if (!load_stl(stl_path, &model)) return -1;
        for (ModelObject *o : model.objects) {
            if (o->instances.empty()) o->add_instance();
            o->ensure_on_bed(); // a pricer upload can sit anywhere in Z
        }

        lap("load_stl");
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
        lap("profiles");
        Print print;
        // Status messages mark the steps inside process() and export.
        print.set_status_callback([](const PrintBase::SlicingStatus &st) { if (!st.text.empty()) lap(("~" + st.text).c_str()); });
        print.set_plate_origin(Vec3d::Zero()); // Print::m_origin is an uninitialised Eigen Vec3d (Print.hpp:1192); GUI/CLI set it via PartPlate
        print.apply(model, config);
        StringObjectException err = print.validate();
        if (!err.string.empty()) { std::fprintf(stderr, "validate: %s\n", err.string.c_str()); }
        lap("apply_validate");
        print.process();
        lap("process");
        GCodeProcessorResult result;
        print.export_gcode(gcode_out, &result, nullptr);
        lap("export_gcode");

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

// The pricer's entry point. Called from the slicer's Web Worker, which keeps
// the module loaded, so profiles are read once per printer and filament.
// overrides: "key\tvalue\n" lines. Returns a small JSON object.
extern "C" EMSCRIPTEN_KEEPALIVE const char *kz_slice(const char *stl_path, const char *profiles, const char *overrides, const char *gcode_out)
{
    // oneTBB on emscripten defaults worker stacks to __TBB_EMSCRIPTEN_STACK_SIZE = 64 KB (onetbb cmake/compilers/Clang.cmake:21),
    // which Arachne's recursive SkeletalTrapezoidation::propagateBeadingsDownward overflows silently -> heap corruption.
    static tbb::global_control stack_ctl(tbb::global_control::thread_stack_size, 4u << 20);
    // TBB must not want more threads than the pthread pool has (PTHREAD_POOL_SIZE
    // uses the same 2..4 clamp); a blocked caller can't grow the pool, so more
    // would deadlock.
    static tbb::global_control threads_ctl(tbb::global_control::max_allowed_parallelism,
                                           std::clamp(emscripten_num_logical_cores(), 2, 4));
    Overrides kv;
    std::string o = overrides ? overrides : "";
    for (size_t pos = 0; pos < o.size();) {
        size_t nl = o.find('\n', pos); if (nl == std::string::npos) nl = o.size();
        std::string line = o.substr(pos, nl - pos); pos = nl + 1;
        size_t tab = line.find('\t');
        if (tab != std::string::npos) kv.emplace_back(line.substr(0, tab), line.substr(tab + 1));
    }
    double t = 0, mm = 0, g = 0;
    int rc = orca_slice(stl_path, profiles, kv, gcode_out, &t, &mm, &g);
    static char out[160];
    std::snprintf(out, sizeof out, "{\"rc\":%d,\"time_s\":%.1f,\"filament_mm\":%.1f,\"filament_g\":%.2f}", rc, t, mm, g);
    return out;
}

// Command line: model profiles gcode_out [key value ...]. KZ_REPEAT=n slices
// the same model n times in one process, to time warm slices.
int main(int argc, char **argv)
{
    if (argc < 4) return 1;
    std::string kv;
    for (int i = 4; i + 1 < argc; i += 2) kv += std::string(argv[i]) + "\t" + argv[i + 1] + "\n";
    const char *rep = std::getenv("KZ_REPEAT");
    int n = rep ? std::max(1, std::atoi(rep)) : 1;
    int rc = 0;
    for (int i = 0; i < n; ++i) {
        const char *r = kz_slice(argv[1], argv[2], kv.c_str(), argv[3]);
        std::printf("%s\n", r);
        rc = std::atoi(r + 6);
    }
    return rc;
}
