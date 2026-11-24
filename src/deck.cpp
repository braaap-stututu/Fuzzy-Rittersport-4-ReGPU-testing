// The Steam Deck profile (after the FM3 port's src/deck.cpp): settings for a Steam Deck under Proton, kept apart from
// what every other PC gets. The general low-end work (Wine waits, XMA codec reuse, RADV's MSAA layouts, the texture
// write watch, DirectInput only for wheels) is on everywhere it applies and isn't here. Each setting is a default, so
// a switch set by the player or a test script still wins. Measurements: DECK-RESULTS.md in the Deck package.
//
// settings.ini deck=auto|on|off (default auto); FM4_DECK=0|1 overrides it.
//   auto: on under Wine/Proton on a Steam Deck APU (CPU "AMD Custom APU 0405", LCD, 60 Hz; "0932", OLED, 90 Hz).
//
// What it sets:
//   frame cap = the screen's refresh rate (60 LCD, 90 OLED) when settings.ini asks for more and FM2_MAX_FPS isn't set:
//                in memory only, settings.ini keeps its value. Frames past the screen's rate are never seen, and on the
//                Deck the APU's 15 W are shared: CPU and GPU work for unseen frames takes clock from the seen ones.
//                (In FM4 the cap is also the physics rate: it is only ever lowered, to a rate the console's 60 Hz
//                physics or more.)
#include "deck.h"

#include "log.h"
#include "settings.h"

#include <intrin.h>
#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
bool g_deck = false;

std::string CpuBrand()
{
    int regs[4]{};
    char brand[49]{};
    __cpuid(regs, int(0x80000000));
    if (unsigned(regs[0]) < 0x80000004u)
        return {};
    for (int i = 0; i < 3; i++)
    {
        __cpuid(regs, int(0x80000002 + i));
        memcpy(brand + i * 16, regs, 16);
    }
    std::string s(brand);
    while (!s.empty() && s.back() == ' ')
        s.pop_back();
    return s;
}

bool OnWine()
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll && GetProcAddress(ntdll, "wine_get_version");
}
} // namespace

bool DeckProfile()
{
    return g_deck;
}

void DeckProfileInit()
{
    const std::string cpu = CpuBrand();
    const bool wine = OnWine();
    const bool lcd = cpu.find("AMD Custom APU 0405") != std::string::npos;
    const bool oled = cpu.find("AMD Custom APU 0932") != std::string::npos;
    std::string mode = GetSettings().Extra("deck");
    if (const char* v = getenv("FM4_DECK"); v && *v)
        mode = strcmp(v, "0") == 0 ? "off" : "on";
    if (mode != "on" && mode != "off")
        mode = "auto";
    g_deck = mode == "on" || (mode == "auto" && wine && (lcd || oled));
    Log("deck: Steam Deck profile %s (deck=%s; CPU \"%s\", %s)", g_deck ? "on" : "off", mode.c_str(), cpu.c_str(),
        wine ? "under Wine/Proton" : "not under Wine");
    if (!g_deck)
        return;
    const uint32_t screenHz = oled || !lcd ? 90 : 60; // (forced on elsewhere: the OLED's)
    if (Settings& st = GetSettings(); st.maxFps > screenHz && !getenv("FM2_MAX_FPS"))
    {
        Log("deck: frame cap %u -> %u (the screen's refresh rate; settings.ini unchanged)", st.maxFps, screenHz);
        st.maxFps = screenHz;
    }
}
