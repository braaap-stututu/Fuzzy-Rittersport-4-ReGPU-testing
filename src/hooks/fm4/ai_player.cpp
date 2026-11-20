// The player's car driven by FM4's own race AI, for unattended benchmarks (after the FM3 port's hooks/ai_player.cpp;
// address in hooks/fm4/addresses.h).
//
// FM4_AI_PLAYER=1, or FM4_BENCHMARK=<seconds> unless FM4_AI_PLAYER=0 (read as each race is built): the player's car
// is built exactly as an opponent's, so the race AI drives it for the whole race, while the HUD, camera, position and
// results still follow it as the player's. FM4_AI_PLAYER_LOG=1 logs every car built.
//
// The car driver setup (car, params) runs once per car as a race is built. car+104 == 0 is the player's car: it
// gets a CHumanDriver (the pad or wheel). Opponents (car+104 == 2, params[0] == 2) get four calls on their physics
// and an AI driver from the factory. Here the player's car goes through the opponent path (car+104 and params[0]
// set to 2 for the call) and is marked the player's again afterwards. FM3 found that the other ways fail: an AI
// driver installed after the game built the car left the pad driving it, and building it as type 3 made an AI that
// revved on the line and never launched.
#include <cstdlib>
#include <cstring>

#include "hooks/fm4/addresses.h"
#include "kernel/imports.h"
#include "log.h"

#ifdef FM4_CAR_DRIVER_SETUP

namespace
{
constexpr uint32_t kCarKind = 104;  // 0 the player's car, 2 an opponent
constexpr uint32_t kActiveDriver = 2296; // the active driver (set through sub_82A923E8: car+2296; FM3 +2280), logged only

bool AiPlayerOn()
{
    const char* v = getenv("FM4_AI_PLAYER");
    if (v && *v)
        return strcmp(v, "0") != 0;
    const char* bench = getenv("FM4_BENCHMARK");
    return bench && atoi(bench) > 0;
}
} // namespace

PPC_FUNC_IMPL(FM4_CAR_DRIVER_SETUP_IMPL);
PPC_FUNC(FM4_CAR_DRIVER_SETUP)
{
    const uint32_t car = ctx.r3.u32, params = ctx.r4.u32;
    const uint32_t kind = LoadBE32(car + kCarKind), type = LoadBE32(params);
    static const bool log = getenv("FM4_AI_PLAYER_LOG") != nullptr;
    if (log)
        Log("ai player: building car %08X: +104 %u, type %u", car, kind, type);
    if (kind != 0 || !AiPlayerOn())
    {
        FM4_CAR_DRIVER_SETUP_IMPL(ctx, base);
        return;
    }
    StoreBE32(car + kCarKind, 2);
    StoreBE32(params, 2);
    FM4_CAR_DRIVER_SETUP_IMPL(ctx, base);
    StoreBE32(params, type);
    StoreBE32(car + kCarKind, 0);
    Log("ai player: the player's car %08X is driven by the race AI (+%u: %08X)", car, kActiveDriver, LoadBE32(car + kActiveDriver));
}

#endif
