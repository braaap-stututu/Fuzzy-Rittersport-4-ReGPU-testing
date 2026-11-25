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
//                (In FM4 the cap is also the physics rate: lowered here only to a rate of the console's 60 Hz
//                physics or more.)
//   presets       for what settings.ini doesn't set (FM4_DECK_PRESETS=0: none): resolution 1280x800 (the panel), frame
//                cap = the screen's refresh rate (90 on the OLED, where the built-in default is 60). See DeckProfileInit.
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
    Settings& st = GetSettings();
    if (st.maxFps > screenHz && !getenv("FM2_MAX_FPS"))
    {
        Log("deck: frame cap %u -> %u (the screen's refresh rate; settings.ini unchanged)", st.maxFps, screenHz);
        st.maxFps = screenHz;
    }
    // Presets: what a Deck player gets for the settings settings.ini doesn't set (a first start, or a settings.ini
    // written by hand), in memory only. FM4_DECK_PRESETS=0: the built-in defaults instead (1280x720, 60 fps).
    //   resolution 1280x800  the Deck's panel (the built-in 1280x720 left bars on it).
    //   max_fps = screen Hz  90 on the OLED (the AI race holds 90.0 at 1280x800, MSAA 1x: DECK-RESULTS.md), 60 on the LCD.
    //   MSAA stays 1x: at 2x the frozen grid makes ~76 fps (short of the OLED's 90); 2x at 60 is a choice left to the player.
    const char* presets = getenv("FM4_DECK_PRESETS");
    if (presets && strcmp(presets, "0") == 0)
        return;
    if (!st.fromFile.count("resolution") && !getenv("FM2_RESOLUTION") && !getenv("FM2_RENDER_SCALE"))
    {
        Log("deck: preset resolution %ux%u -> 1280x800 (the panel; not set in settings.ini)", st.width, st.height);
        st.width = 1280, st.height = 800;
    }
    if (!st.fromFile.count("max_fps") && !getenv("FM2_MAX_FPS") && st.maxFps != screenHz)
    {
        Log("deck: preset frame cap %u -> %u (the screen's refresh rate; not set in settings.ini)", st.maxFps, screenHz);
        st.maxFps = screenHz;
    }
}
