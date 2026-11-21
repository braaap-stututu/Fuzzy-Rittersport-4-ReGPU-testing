// GPU wait without spinning (after the FM3 port's hooks/ring_wait.cpp; address in hooks/fm4/addresses.h).
//
// When ReGPU is behind (GPU-bound), the game's render thread (guest 7) waits in D3D's wait loop, whose round
// FM4_GPU_WAIT_ROUND (32 db16cyc delays and a check) answers 1 to go on waiting: the thread spun, ~20% of its samples
// in the round itself in a race, a whole core busy for nothing (a core taken from the command processor, the draw
// thread and the game's main thread on 4-core CPUs such as the Steam Deck's, and power on laptops).
//
// FM4_RING_WAIT (read 4 times a second, so the debug server's "env FM4_RING_WAIT 0" flips it live):
//   0          as the game does it (spin)
//   1          SwitchToThread() after each round that goes on waiting
//   2 / unset  sleep ~100 us on a high-resolution waitable timer after each such round (gives the core back)
// The answer (r3) and everything the round does are the game's; only the time between rounds changes.
// FM3 measured mode 2: -17-19% CPU, same fps and frame times.
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdlib>

#include "hooks/fm4/addresses.h"
#include "kernel/imports.h"
#include "log.h"

#ifdef FM4_GPU_WAIT_ROUND

namespace
{
int Mode()
{
    static std::atomic<int> mode{ -1 }; // (-1: not read yet, so the first read logs)
    static std::atomic<int64_t> next{ 0 };
    const int64_t now = std::chrono::steady_clock::now().time_since_epoch().count();
    if (now >= next.load(std::memory_order_relaxed))
    {
        next.store(now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::milliseconds(250)).count(),
            std::memory_order_relaxed);
        char v[8] = {};
        const int m = GetEnvironmentVariableA("FM4_RING_WAIT", v, sizeof(v)) && v[0] ? atoi(v) : 2;
        if (m != mode.exchange(m, std::memory_order_relaxed))
            Log("ring wait: mode %d (%s)", m, m == 1 ? "yield" : m == 2 ? "sleep 100 us" : "spin, as the game does");
    }
    return mode.load(std::memory_order_relaxed);
}

void ShortSleep()
{
    thread_local HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer)
    {
        SwitchToThread();
        return;
    }
    LARGE_INTEGER due;
    due.QuadPart = -1000; // 100 us, relative, in 100 ns units
    if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE))
        WaitForSingleObject(timer, 1);
}
} // namespace

PPC_FUNC_IMPL(FM4_GPU_WAIT_ROUND_IMPL);
PPC_FUNC(FM4_GPU_WAIT_ROUND)
{
    FM4_GPU_WAIT_ROUND_IMPL(ctx, base);
    if (ctx.r3.u32 != 1)
        return;
    switch (Mode())
    {
    case 1:
        SwitchToThread();
        break;
    case 2:
        ShortSleep();
        break;
    default:
        break;
    }
}

#endif
