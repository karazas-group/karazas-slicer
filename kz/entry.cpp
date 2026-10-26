// Stage: entry. The pricer's one exported function, kz_slice(), and a command
// line main() for running a slice outside the browser. Owns thread limits and
// the parsing of the "key\tvalue\n" override string; nothing else.
#include "kz.hpp"
#include <tbb/global_control.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <emscripten.h>
#include <emscripten/threading.h>

// The pricer's entry point. Called from the slicer's Web Worker, which keeps
// the module loaded, so profiles are read once per printer and filament.
// overrides: "key\tvalue\n" lines. Returns a small JSON object.
extern "C" EMSCRIPTEN_KEEPALIVE const char *kz_slice(const char *stl_path, const char *profiles, const char *overrides, const char *gcode_out)
{
    // oneTBB on emscripten defaults worker stacks to __TBB_EMSCRIPTEN_STACK_SIZE = 64 KB (onetbb cmake/compilers/Clang.cmake:21),
    // which Arachne's recursive SkeletalTrapezoidation::propagateBeadingsDownward overflows silently -> heap corruption.
    static tbb::global_control stack_ctl(tbb::global_control::thread_stack_size, 4u << 20);
    // TBB must not want more threads than the pthread pool has (PTHREAD_POOL_SIZE
    // uses the same 2..8 clamp); a blocked caller can't grow the pool, so more
    // would deadlock.
    static tbb::global_control threads_ctl(tbb::global_control::max_allowed_parallelism,
                                           std::clamp(emscripten_num_logical_cores(), 2, 8));
    kz::Overrides kv;
    std::string o = overrides ? overrides : "";
    for (size_t pos = 0; pos < o.size();) {
        size_t nl = o.find('\n', pos); if (nl == std::string::npos) nl = o.size();
        std::string line = o.substr(pos, nl - pos); pos = nl + 1;
        size_t tab = line.find('\t');
        if (tab != std::string::npos) kv.emplace_back(line.substr(0, tab), line.substr(tab + 1));
    }
    double t = 0, mm = 0, g = 0;
    int rc = kz::slice_one(stl_path, profiles, kv, gcode_out, &t, &mm, &g);
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
