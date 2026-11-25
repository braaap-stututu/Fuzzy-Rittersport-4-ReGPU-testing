// Lock and address-wake call counts by call site (FM4_LOCK_STATS=1, start-time; a diagnostic, off by default).
//
// Steam Deck, 22 Nov 2026: ntdll's address wakes were ~9% of a race's CPU samples (Wine's SRW release wakes the
// address every time), and perf can't walk the stacks of Win64 code to say who calls them. This wraps fm4.exe's own
// imports of the lock, condition variable and address-wait functions (the C++ runtime is linked in, so std::mutex,
// std::condition_variable and std::atomic::wait/notify come through them too), counts every call and, for one call in
// 64 per thread, records the call stack (CaptureStackBackTrace, 6 frames). Every 10 s the log gets the calls per second
// of each function and the busiest stacks as fm4.exe offsets (llvm-symbolizer --obj=fm4.exe --pdb=fm4.pdb 0x...).
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "log.h"

namespace
{
enum Fn
{
    kAcquireX,
    kReleaseX,
    kAcquireS,
    kReleaseS,
    kTryAcquireX,
    kWakeSingle,
    kWakeAll,
    kWaitOnAddress,
    kCvWake,
    kCvWakeAll,
    kCvSleep,
    kCsEnter,
    kCsLeave,
    kFnCount
};
const char* const kNames[kFnCount] = { "AcquireSRWLockExclusive", "ReleaseSRWLockExclusive", "AcquireSRWLockShared",
    "ReleaseSRWLockShared", "TryAcquireSRWLockExclusive", "WakeByAddressSingle", "WakeByAddressAll", "WaitOnAddress",
    "WakeConditionVariable", "WakeAllConditionVariable", "SleepConditionVariableSRW", "EnterCriticalSection",
    "LeaveCriticalSection" };

constexpr int kFrames = 6;
constexpr uint32_t kSample = 64;
constexpr uint32_t kSites = 8192;
struct Site
{
    std::atomic<uint64_t> key{ 0 };
    uint32_t fn = 0;
    uint64_t frames[kFrames]{};
    std::atomic<uint32_t> count{ 0 };
};
Site g_sites[kSites];
std::atomic<uint64_t> g_calls[kFnCount];
uintptr_t g_base = 0, g_size = 0;

void Note(Fn fn)
{
    g_calls[fn].fetch_add(1, std::memory_order_relaxed);
    thread_local uint32_t n = 0;
    thread_local bool inside = false;
    if ((++n % kSample) != 0 || inside)
        return;
    inside = true;
    void* frames[kFrames + 2]{};
    const USHORT got = RtlCaptureStackBackTrace(2, kFrames, frames, nullptr); // (past Note and the wrapper)
    uint64_t key = 0xCBF29CE484222325ull ^ fn;
    for (USHORT i = 0; i < got; i++)
        key = (key ^ uint64_t(uintptr_t(frames[i]))) * 0x100000001B3ull;
    key |= 1;
    for (uint32_t i = 0, h = uint32_t(key >> 20) % kSites; i < 64; i++, h = (h + 1) % kSites)
    {
        Site& s = g_sites[h];
        uint64_t k = s.key.load(std::memory_order_acquire);
        if (k == 0)
        {
            // Claim the slot: frames are written before the key is published with its count.
            uint64_t expected = 0;
            if (s.key.compare_exchange_strong(expected, key, std::memory_order_acq_rel) || expected == key)
            {
                if (expected == 0)
                {
                    s.fn = fn;
                    for (USHORT f = 0; f < got; f++)
                        s.frames[f] = uintptr_t(frames[f]);
                }
                s.count.fetch_add(1, std::memory_order_relaxed);
                break;
            }
            k = expected;
        }
        if (k == key)
        {
            s.count.fetch_add(1, std::memory_order_relaxed);
            break;
        }
    }
    inside = false;
}

decltype(&AcquireSRWLockExclusive) o_acquireX;
decltype(&ReleaseSRWLockExclusive) o_releaseX;
decltype(&AcquireSRWLockShared) o_acquireS;
decltype(&ReleaseSRWLockShared) o_releaseS;
decltype(&TryAcquireSRWLockExclusive) o_tryAcquireX;
decltype(&WakeByAddressSingle) o_wakeSingle;
decltype(&WakeByAddressAll) o_wakeAll;
decltype(&WaitOnAddress) o_waitOnAddress;
decltype(&WakeConditionVariable) o_cvWake;
decltype(&WakeAllConditionVariable) o_cvWakeAll;
decltype(&SleepConditionVariableSRW) o_cvSleep;
decltype(&EnterCriticalSection) o_csEnter;
decltype(&LeaveCriticalSection) o_csLeave;

void WINAPI W_AcquireX(PSRWLOCK l) { Note(kAcquireX); o_acquireX(l); }
void WINAPI W_ReleaseX(PSRWLOCK l) { Note(kReleaseX); o_releaseX(l); }
void WINAPI W_AcquireS(PSRWLOCK l) { Note(kAcquireS); o_acquireS(l); }
void WINAPI W_ReleaseS(PSRWLOCK l) { Note(kReleaseS); o_releaseS(l); }
BOOLEAN WINAPI W_TryAcquireX(PSRWLOCK l) { Note(kTryAcquireX); return o_tryAcquireX(l); }
void WINAPI W_WakeSingle(PVOID a) { Note(kWakeSingle); o_wakeSingle(a); }
void WINAPI W_WakeAll(PVOID a) { Note(kWakeAll); o_wakeAll(a); }
BOOL WINAPI W_WaitOnAddress(volatile VOID* a, PVOID c, SIZE_T s, DWORD ms) { Note(kWaitOnAddress); return o_waitOnAddress(a, c, s, ms); }
void WINAPI W_CvWake(PCONDITION_VARIABLE c) { Note(kCvWake); o_cvWake(c); }
void WINAPI W_CvWakeAll(PCONDITION_VARIABLE c) { Note(kCvWakeAll); o_cvWakeAll(c); }
BOOL WINAPI W_CvSleep(PCONDITION_VARIABLE c, PSRWLOCK l, DWORD ms, ULONG f) { Note(kCvSleep); return o_cvSleep(c, l, ms, f); }
void WINAPI W_CsEnter(LPCRITICAL_SECTION c) { Note(kCsEnter); o_csEnter(c); }
void WINAPI W_CsLeave(LPCRITICAL_SECTION c) { Note(kCsLeave); o_csLeave(c); }

struct Hook
{
    const char* name;
    void* wrapper;
    void** original;
};
const Hook kHooks[] = {
    { "AcquireSRWLockExclusive", (void*)&W_AcquireX, (void**)&o_acquireX },
    { "ReleaseSRWLockExclusive", (void*)&W_ReleaseX, (void**)&o_releaseX },
    { "AcquireSRWLockShared", (void*)&W_AcquireS, (void**)&o_acquireS },
    { "ReleaseSRWLockShared", (void*)&W_ReleaseS, (void**)&o_releaseS },
    { "TryAcquireSRWLockExclusive", (void*)&W_TryAcquireX, (void**)&o_tryAcquireX },
    { "WakeByAddressSingle", (void*)&W_WakeSingle, (void**)&o_wakeSingle },
    { "WakeByAddressAll", (void*)&W_WakeAll, (void**)&o_wakeAll },
    { "WaitOnAddress", (void*)&W_WaitOnAddress, (void**)&o_waitOnAddress },
    { "WakeConditionVariable", (void*)&W_CvWake, (void**)&o_cvWake },
    { "WakeAllConditionVariable", (void*)&W_CvWakeAll, (void**)&o_cvWakeAll },
    { "SleepConditionVariableSRW", (void*)&W_CvSleep, (void**)&o_cvSleep },
    { "EnterCriticalSection", (void*)&W_CsEnter, (void**)&o_csEnter },
    { "LeaveCriticalSection", (void*)&W_CsLeave, (void**)&o_csLeave },
};

int PatchImports()
{
    const auto base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    g_base = uintptr_t(base);
    const auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    const auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    g_size = nt->OptionalHeader.SizeOfImage;
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    int patched = 0;
    for (auto imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; imp++)
    {
        auto names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        auto slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; names++, slots++)
        {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                continue;
            const char* name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name;
            for (const Hook& h : kHooks)
            {
                if (strcmp(name, h.name) != 0)
                    continue;
                DWORD old = 0;
                VirtualProtect(&slots->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
                *h.original = reinterpret_cast<void*>(slots->u1.Function);
                slots->u1.Function = reinterpret_cast<ULONG_PTR>(h.wrapper);
                VirtualProtect(&slots->u1.Function, sizeof(void*), old, &old);
                patched++;
            }
        }
    }
    return patched;
}

void Report(double seconds)
{
    char line[1024];
    int at = snprintf(line, sizeof(line), "lock stats: calls/s");
    for (int f = 0; f < kFnCount; f++)
        if (const uint64_t n = g_calls[f].exchange(0))
            at += snprintf(line + at, sizeof(line) - at, " %s %.0f", kNames[f], double(n) / seconds);
    Log("%s", line);
    // The busiest sampled stacks (estimated calls/s), cleared for the next window.
    struct Top
    {
        uint32_t count;
        Site* site;
    } top[30]{};
    for (Site& s : g_sites)
    {
        if (!s.key.load(std::memory_order_acquire))
            continue;
        const uint32_t c = s.count.exchange(0);
        for (int i = 0; i < 30; i++)
            if (c > top[i].count)
            {
                memmove(&top[i + 1], &top[i], sizeof(Top) * (29 - i));
                top[i] = { c, &s };
                break;
            }
    }
    for (const Top& t : top)
    {
        if (!t.site)
            break;
        at = snprintf(line, sizeof(line), "lock stats: %8.0f/s %-26s", double(t.count) * kSample / seconds, kNames[t.site->fn]);
        for (uint64_t f : t.site->frames)
            if (f)
                at += snprintf(line + at, sizeof(line) - at, f >= g_base && f < g_base + g_size ? " 0x%llx" : " ext:%llx",
                    (unsigned long long)(f >= g_base && f < g_base + g_size ? f - g_base : f));
        Log("%s", line);
    }
}
}

void LockStatsInit()
{
    const char* v = getenv("FM4_LOCK_STATS");
    if (!v || !*v || strcmp(v, "0") == 0)
        return;
    const int patched = PatchImports();
    Log("lock stats: %d imports wrapped (sampled stacks every %u calls a thread; offsets from the image base)", patched, kSample);
    std::thread([] {
        for (;;)
        {
            Sleep(10000);
            Report(10.0);
        }
    }).detach();
}
