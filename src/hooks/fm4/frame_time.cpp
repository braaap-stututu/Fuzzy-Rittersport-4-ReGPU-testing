// The main loop's frame time, for test tools (after the FM3 port's hooks/frame_time.cpp; FM3's engine is FM4's
// predecessor and paces itself the same way; addresses in hooks/fm4/addresses.h).
//
// The main loop takes the milliseconds since its last frame (r30; over 4000 counts as 16) and f31 = r30 * 0.001,
// then calls the frame-time check: true = use f31 as it is, false = snap 16/17 ms frames to 1/59.94 s and carry
// the remainder. This hook runs the check, then may replace its answer:
// - FM4_FREEZE_TIME=1 (or the test-grid route, race_start.cpp): f31 = 0 and true, so the world stops (cars,
//   cameras, countdown, race clock: everything the main loop steps with dt) while every frame is still drawn:
//   screenshots within a boot are steady, for picture and speed A/Bs without the pause menu.
// - FM4_FIXED_DT=<fps> (e.g. 60): f31 = exactly 1/fps and true, so game time is the frame count.
// Both are read once a second from the environment, so the debug server flips them live
// (regpuctl.py "env FM4_FREEZE_TIME 1", "env FM4_FREEZE_TIME" to clear). Main frames of 250 ms or more are
// logged as loads.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>

#include <windows.h>

#include "hooks/fm4/addresses.h"
#include "hooks/fm4/frame_time.h"
#include "kernel/imports.h"
#include "log.h"

#ifdef FM4_MAIN_LOOP_FRAME_TIME_CHECK

namespace
{
// An environment variable read at most once a second: its numeric value (unset or empty = 0).
struct EnvValue
{
    const char* name;
    std::atomic<double> value{ 0.0 };
    std::atomic<int64_t> next{ 0 };
    explicit EnvValue(const char* n) : name(n) {}
    double Get()
    {
        const int64_t now = std::chrono::steady_clock::now().time_since_epoch().count();
        if (now >= next.load(std::memory_order_relaxed))
        {
            char v[32] = {};
            const DWORD n = GetEnvironmentVariableA(name, v, sizeof(v));
            value.store(n && n < sizeof(v) ? atof(v) : 0.0, std::memory_order_relaxed);
            next.store(now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::seconds(1)).count(),
                std::memory_order_relaxed);
        }
        return value.load(std::memory_order_relaxed);
    }
};

std::atomic<bool> g_freezeByRoute{ false };
std::atomic<uint64_t> g_mainFrames{ 0 };
} // namespace

void FrameTimeFreeze(bool on)
{
    g_freezeByRoute.store(on, std::memory_order_relaxed);
}

uint64_t FrameTimeMainFrames()
{
    return g_mainFrames.load(std::memory_order_relaxed);
}

PPC_FUNC_IMPL(FM4_MAIN_LOOP_FRAME_TIME_CHECK_IMPL);
PPC_FUNC(FM4_MAIN_LOOP_FRAME_TIME_CHECK)
{
    const bool fromMainLoop = ctx.lr == FM4_FRAME_TIME_CHECK_RETURN;
    const uint32_t ms = ctx.r30.u32; // the main loop's r30, kept by the callee (non-volatile)
    FM4_MAIN_LOOP_FRAME_TIME_CHECK_IMPL(ctx, base);
    if (!fromMainLoop)
        return;

    const uint64_t frame = g_mainFrames.fetch_add(1, std::memory_order_relaxed) + 1;
    if (ms >= 250)
        Log("frame time: main frame %llu took %u ms (a load)", (unsigned long long)frame, ms);

    // The route's freeze also sets FM4_FREEZE_TIME=1, so clearing the variable lets the world go on either way.
    static EnvValue freeze("FM4_FREEZE_TIME");
    static EnvValue fixedFps("FM4_FIXED_DT");
    static bool switchWasOn = false;
    const bool switchOn = freeze.Get() != 0.0;
    if (switchWasOn && !switchOn)
        g_freezeByRoute.store(false, std::memory_order_relaxed);
    switchWasOn = switchOn;
    const bool frozen = switchOn || g_freezeByRoute.load(std::memory_order_relaxed);
    static bool wasFrozen = false;
    if (frozen != wasFrozen)
    {
        wasFrozen = frozen;
        Log("frame time: world %s at main frame %llu", frozen ? "frozen (dt 0)" : "running again", (unsigned long long)frame);
    }
    if (frozen)
    {
        ctx.f31.f64 = 0.0;
        ctx.r3.u64 = 1;
    }
    else if (const double fps = fixedFps.Get(); fps >= 1.0)
    {
        ctx.f31.f64 = double(float(1.0 / fps));
        ctx.r3.u64 = 1;
    }
}

#endif
