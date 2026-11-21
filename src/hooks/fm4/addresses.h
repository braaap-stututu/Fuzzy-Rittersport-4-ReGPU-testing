#pragma once
// FM4 guest addresses the runtime hooks replace, from FM4's own default.xex (they differ from
// FM2's); each is a function the recompiled image defines as sub_XXXXXXXX.
//
// XAPI fibers: statically linked XDK code. The title's SwitchToFiber swaps the stack pointer and
// registers by hand, which recompiled code cannot follow (hooks/fm4/fibers.cpp replaces them
// with host fibers). Located by tools/findfibers.py and checked against the disassembly:
//   ConvertThreadToFiber  allocates the 0xA50-byte fiber object, sets KTHREAD+0x164
//   ConvertFiberToThread  clears KTHREAD+0x164, frees the object
//   CreateFiber           allocates the object plus a stack of at least 16 KB
//   DeleteFiber           on the current fiber it converts back and exits the thread
//   SwitchToFiber         0x82C85380 includes the register swap (0x82C85394..0x82C85788);
//                         callers reach it through the stub at 0x82C82C10 (b 0x82C85380), so
//                         there is no separate swap routine to hook. The start trampoline at
//                         0x82C82C20 is replaced by FiberMain.
#define FM4_XAPI_FIBER_ADDRESSES
#define FM4_XAPI_CONVERT_THREAD_TO_FIBER sub_82C829F0
#define FM4_XAPI_CONVERT_FIBER_TO_THREAD sub_82C82A80
#define FM4_XAPI_CREATE_FIBER            sub_82C82AD0
#define FM4_XAPI_DELETE_FIBER            sub_82C82BB0
#define FM4_XAPI_SWITCH_TO_FIBER         sub_82C85380

// Test tools (hooks/fm4/frame_time.cpp, race_start.cpp, ai_player.cpp; docs: FM4-BENCHMARK-RESEARCH.md in the
// workspace). Found by matching the FM3 port's hooks (FM3's engine is FM4's predecessor) through the same
// strings and RTTI classes; the FM3 counterparts are named after each.
//   Main loop sub_822F74A0 (FM3 sub_821F5DE8): after waiting for its frame, r30 = milliseconds since the last
//     frame (over 4000: 16), f31 = r30 * 0.001, then the frame-time check below; true = take f31 as it is,
//     false = the 16/17 ms snap to 1/59.94 with a carried remainder. The frame time is stored at +408.
//   Frame-time check sub_822D3EB8 (FM3 sub_821F6AF8), called from the main loop with this return address.
//   Race-start sequence update sub_8239D018 (FM3 sub_822E6AD0): r3 = the sequence, f1 = dt; the only function
//     reading both OverrideRevsDuration (0x82DDB280) and PreRaceCamsFadeOut (0x82DDB320) of 321Tuning.xml.
//     Countdown clock: double at +16; flags: bytes +24..+32 (FM3: +24 intro, +25 start taken, +26 GO; FM4's
//     function starts on +26 set and +27 clear where FM3's starts on +25 set and +26 clear).
//   Car driver setup sub_82A9E110(car, params) (FM3 sub_828F8858): car+104 == 0 (the player's car) makes a
//     CHumanDriver (ctor sub_82A92A80, vtable 0x82002360); opponents get the physics calls on car+2072 and an AI
//     driver from the factory sub_82A9DE48(car, params); params[0] is the entry's type (3 player, 2 opponent).
#define FM4_MAIN_LOOP_FRAME_TIME_CHECK      sub_822D3EB8
#define FM4_MAIN_LOOP_FRAME_TIME_CHECK_IMPL __imp__sub_822D3EB8
#define FM4_FRAME_TIME_CHECK_RETURN         0x822F776Cu
#define FM4_RACE_START_UPDATE               sub_8239D018
#define FM4_RACE_START_UPDATE_IMPL          __imp__sub_8239D018
#define FM4_CAR_DRIVER_SETUP                sub_82A9E110
#define FM4_CAR_DRIVER_SETUP_IMPL           __imp__sub_82A9E110
//   D3D's GPU wait round sub_826EDE50 (FM3 sub_824AAA68): 32 db16cyc delays and a check of the command processor's
//     progress; answers r3 = 1 to go on waiting. Its callers loop on it (the render thread waiting for ring space or
//     a fence): hooks/fm4/ring_wait.cpp sleeps between rounds.
#define FM4_GPU_WAIT_ROUND                  sub_826EDE50
#define FM4_GPU_WAIT_ROUND_IMPL             __imp__sub_826EDE50
