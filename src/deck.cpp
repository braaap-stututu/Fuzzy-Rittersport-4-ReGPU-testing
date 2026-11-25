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
//
// Toggles (the launcher's "Deck optimizations" tab, ApplyDeckToggles): settings.ini deck_presets, deck_texture_watch,
// deck_wine_waits, deck_xma_reuse, deck_msaa_layouts, deck_wheel_input = 0 turn each optimisation off.
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

// The launcher's "Deck optimizations" tab: one settings.ini key per optimisation (1 = on, the default; 0 = off), each
// turned into the switch the code already reads. A switch set in the environment (a test script, the player) wins.
// deck_xalia (PROTON_USE_XALIA) and deck_pgo (which exe starts) are the launcher's and the play scripts'.
struct DeckToggle
{
    const char* key;    // settings.ini
    const char* env;    // the switch
    const char* offVal; // its value with the key at 0
    const char* what;
};
constexpr DeckToggle kToggles[] = {
    {"deck_presets", "FM4_DECK_PRESETS", "0", "Deck presets (1280x800, frame cap = screen Hz)"},
    {"deck_texture_watch", "REGPU_TEXTURE_NO_WATCH", "1", "texture write watch"},
    {"deck_wine_waits", "REGPU_WINE_WAITS", "0", "Wine/Proton waits"},
    {"deck_xma_reuse", "FM2_XMA_REUSE", "0", "XMA codec reuse"},
    {"deck_msaa_layouts", "REGPU_VK_NO_MSAA_GENERAL", "0", "MSAA layouts by driver"},
    {"deck_wheel_input", "FM4_WHEEL_DI_ALL", "1", "DirectInput only for wheels"},
};

void ApplyDeckToggles()
{
    const Settings& st = GetSettings();
    for (const DeckToggle& t : kToggles)
    {
        if (st.Extra(t.key) != "0")
            continue;
        if (const char* v = getenv(t.env); v && *v)
        {
            Log("deck: %s off in settings.ini, but %s=%s is set: kept", t.what, t.env, v);
            continue;
        }
        _putenv_s(t.env, t.offVal);
        Log("deck: %s off (settings.ini %s=0: %s=%s)", t.what, t.key, t.env, t.offVal);
    }
}
} // namespace

bool DeckProfile()
{
    return g_deck;
}

void DeckProfileInit()
{
    ApplyDeckToggles(); // (first: they apply with the profile on or off, and the presets below read FM4_DECK_PRESETS)
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
