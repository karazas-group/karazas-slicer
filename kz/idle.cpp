// Idle meter: thread time spent blocked in emscripten_futex_wait, summed over
// all threads. Linked with -Wl,--wrap=emscripten_futex_wait (KZ_IDLE builds
// only). kz::lap prints the idle ms of each phase next to its wall ms.
// A wait still in progress counts up to now, so a thread asleep through a
// whole phase is charged to that phase, not to the one in which it wakes.
// Spinning before a thread blocks counts as busy, so this is a lower bound.
#include "kz.hpp"
#include <atomic>
#include <cstdint>
#include <emscripten.h>

static constexpr int kSlots = 32;
static std::atomic<int64_t> g_done_us{0};          // finished waits
static std::atomic<int64_t> g_start_us[kSlots];    // start of the wait in progress per thread, 0 if none
static std::atomic<int> g_next_slot{0};
static thread_local int t_slot = -1;

static int64_t now_us() { return int64_t(emscripten_get_now() * 1000.0); }

extern "C" int __real_emscripten_futex_wait(volatile void *addr, uint32_t val, double maxWaitMs);
extern "C" int __wrap_emscripten_futex_wait(volatile void *addr, uint32_t val, double maxWaitMs)
{
    if (t_slot < 0) t_slot = g_next_slot.fetch_add(1) % kSlots;
    int64_t t = now_us();
    g_start_us[t_slot].store(t);
    int r = __real_emscripten_futex_wait(addr, val, maxWaitMs);
    int64_t e = now_us();
    g_done_us.fetch_add(e - t);
    g_start_us[t_slot].store(0);
    return r;
}

double kz::idle_ms()
{
    int64_t now = now_us(), sum = g_done_us.load();
    for (auto &s : g_start_us) { int64_t t = s.load(); if (t) sum += now - t; }
    return sum / 1000.0;
}
