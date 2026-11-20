// Test grid and benchmark race, driven by the race-start sequence (after the FM3 port's hooks/grid_test.cpp;
// addresses in hooks/fm4/addresses.h). The route to the race is not here: reach it with an FM2_TOUR script or by
// hand; this file acts once a race starts.
//
// - Always: each change of the sequence's flag bytes (+24..+32) is logged with its countdown clock (double at +16),
//   so the flags' meanings and the freeze point can be read off a log (FM4_RACE_START_LOG=0: off).
// - FM4_GRID_TEST=1: the world is frozen (hooks/fm4/frame_time.cpp) when the countdown clock reaches
//   FM4_GRID_FREEZE_AT seconds (default 9.0, FM3's value: to be calibrated for FM4) with the start under way (+26
//   set) and GO not reached (+27 clear), once per boot. FM4_FREEZE_TIME is set too, so clearing it through the
//   debug server ("env FM4_FREEZE_TIME") lets the race go on.
// - FM4_BENCHMARK=<seconds>: at GO (+27 set) a thread waits FM4_BENCHMARK_SETTLE seconds (default 10), logs
//   "benchmark: measuring N s", waits N seconds, logs "benchmark: done" and closes the game's window. The race AI
//   drives the player's car (hooks/fm4/ai_player.cpp) unless FM4_AI_PLAYER=0.
//
// Flag mapping from FM3 (to confirm with the log): FM3's update runs its main part while +25 (start taken) is set
// and +26 (GO) clear; FM4's checks +26 and +27 the same way, so FM3 +25/+26 = FM4 +26/+27, and both read +24
// (FM3: the intro) when the race kind is 3 (career).
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <thread>

#include <windows.h>

#include "hooks/fm4/addresses.h"
#include "hooks/fm4/frame_time.h"
#include "kernel/imports.h"
#include "log.h"

#ifdef FM4_RACE_START_UPDATE

namespace
{
constexpr uint32_t kClock = 16;    // double: the countdown clock (s)
constexpr uint32_t kFlags = 24;    // bytes +24..+32
constexpr uint32_t kFlagCount = 9;
constexpr uint32_t kStarted = 26;  // FM3 +25: Start Race taken, the sequence running
constexpr uint32_t kGo = 27;       // FM3 +26: GO

double LoadBEDouble(uint32_t address)
{
    const uint64_t bits = LoadBE64(address);
    double d;
    memcpy(&d, &bits, sizeof(d));
    return d;
}

int EnvInt(const char* name, int fallback)
{
    const char* v = getenv(name);
    return v && *v ? atoi(v) : fallback;
}

int BenchmarkSeconds()
{
    static const int s = std::max(0, EnvInt("FM4_BENCHMARK", 0));
    return s;
}

BOOL CALLBACK CloseOurWindow(HWND hwnd, LPARAM)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr)
        PostMessageA(hwnd, WM_CLOSE, 0, 0); // what the window's close button does
    return TRUE;
}

void StartBenchmarkClock()
{
    std::thread([] {
        const int settle = std::max(0, EnvInt("FM4_BENCHMARK_SETTLE", 10));
        Sleep(DWORD(settle) * 1000);
        Log("benchmark: measuring %d s (from %d s after GO)", BenchmarkSeconds(), settle);
        Sleep(DWORD(BenchmarkSeconds()) * 1000);
        Log("benchmark: done");
        Sleep(1500); // one more performance line, and the log written
        EnumWindows(CloseOurWindow, 0);
    }).detach();
}

enum class Route { Waiting, Frozen, Racing };
std::atomic<Route> g_route{ Route::Waiting };
} // namespace

PPC_FUNC_IMPL(FM4_RACE_START_UPDATE_IMPL);
PPC_FUNC(FM4_RACE_START_UPDATE)
{
    const uint32_t self = ctx.r3.u32;
    FM4_RACE_START_UPDATE_IMPL(ctx, base);

    uint8_t flags[kFlagCount];
    for (uint32_t i = 0; i < kFlagCount; i++)
        flags[i] = *GuestPtr<uint8_t>(self + kFlags + i);
    const double clock = LoadBEDouble(self + kClock);
    const bool started = flags[kStarted - kFlags] != 0, go = flags[kGo - kFlags] != 0;

    static const bool logFlags = EnvInt("FM4_RACE_START_LOG", 1) != 0;
    static uint32_t lastSelf = 0;
    static uint8_t last[kFlagCount] = {};
    if (logFlags && (self != lastSelf || memcmp(flags, last, kFlagCount) != 0))
    {
        lastSelf = self;
        memcpy(last, flags, kFlagCount);
        Log("race start: %08X flags +24..+32 %u %u %u %u %u %u %u %u %u, clock %.3f s (main frame %llu)", self, flags[0], flags[1],
            flags[2], flags[3], flags[4], flags[5], flags[6], flags[7], flags[8], clock, (unsigned long long)FrameTimeMainFrames());
    }

    const Route route = g_route.load(std::memory_order_relaxed);
    if (route != Route::Waiting)
        return;
    if (BenchmarkSeconds())
    {
        if (go)
        {
            g_route = Route::Racing;
            Log("benchmark: GO at %.3f s of the countdown clock; measuring %d s after %d s", clock, BenchmarkSeconds(),
                std::max(0, EnvInt("FM4_BENCHMARK_SETTLE", 10)));
            StartBenchmarkClock();
        }
        return;
    }
    static const bool gridTest = EnvInt("FM4_GRID_TEST", 0) != 0;
    static const double freezeAt = getenv("FM4_GRID_FREEZE_AT") ? atof(getenv("FM4_GRID_FREEZE_AT")) : 9.0;
    if (gridTest && started && !go && clock >= freezeAt)
    {
        g_route = Route::Frozen;
        FrameTimeFreeze(true);
        SetEnvironmentVariableA("FM4_FREEZE_TIME", "1");
        _putenv_s("FM4_FREEZE_TIME", "1");
        Log("grid test: frozen at %.3f s of the countdown clock (main frame %llu); \"env FM4_FREEZE_TIME\" lets it go on", clock,
            (unsigned long long)FrameTimeMainFrames());
    }
}

#endif
