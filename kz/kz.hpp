// Interfaces between the kz stages. One stage = one .cpp in this directory;
// stages talk through these declarations and nothing else.
#pragma once
#include <chrono>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r { class Print; }

namespace kz {
using Overrides = std::vector<std::pair<std::string, std::string>>;

#ifdef KZ_IDLE
double idle_ms(); // idle.cpp: thread-ms blocked in futex waits so far
#endif

// Phase timer: prints "T <phase> <ms>" to stderr so a run can be split into
// steps. With KZ_IDLE also the idle thread-ms of the phase: "T <phase> <ms> <idle>".
inline void lap(const char *phase)
{
    static auto t0 = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
#ifdef KZ_IDLE
    static double i0 = 0;
    double i = idle_ms();
    std::fprintf(stderr, "T %s %.0f %.0f\n", phase, std::chrono::duration<double, std::milli>(now - t0).count(), i - i0);
    i0 = i;
#else
    std::fprintf(stderr, "T %s %.0f\n", phase, std::chrono::duration<double, std::milli>(now - t0).count());
#endif
    t0 = now;
}

// pipeline.cpp: STL + profiles + overrides -> grams. Returns 0 or a negative error.
int slice_one(const char *stl_path, const char *profiles, const Overrides &overrides, const char *gcode_out,
              double *out_time_s, double *out_filament_mm, double *out_filament_g);
} // namespace kz

// volume_sum.cpp: mm^3 of filament the toolpaths of a processed Print lay down,
// before filament_flow_ratio.
double kz_extruded_volume(Slic3r::Print &print);
