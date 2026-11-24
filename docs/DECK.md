# FM4 on the Steam Deck: optimisation results (22 Nov 2026)

For the PC session: what was measured on the Deck, what was changed, and what to carry back. Briefing:
`docs/DECK-OPTIMIZATION.md`. The user's aim: optimise for low-end hardware in general (the Deck doesn't hide costs behind
brute force), with Deck-specific presets on top.

## Setup

- Steam Deck **OLED** (Galileo, "AMD Custom APU 0932"), SteamOS, Proton Experimental (play.sh's choice), ntsync on.
  Desktop Mode, on AC power, default TDP/clocks (no manual limits: what players get). Room temperature; the APU sat at
  60-61 C and 14-15 W in every run.
- **The Deck build is built on the Deck**: clang-cl + lld-link + xwin (MSVC 14.44, SDK 26100) in an Arch distrobox
  (the FM3 Deck work's toolchain, `ReGPU-V/work/scripts/deck`). FM4 `regpu-vulkan` 464782b + ReGPU `vulkan` a8abaa9,
  the game code recompiled on the Deck with XenonRecomp (+ the five patches in `patches/`) from the disc's XEX files
  (hashes match `tools/recomp-fm4.sh`; 50,425 functions). The cross-built exe with the changes off measures the same as
  this package's `fm4.exe` (grid 61.9 / 62.8 against 61.8 / 61.9 fps).
- Disc files: extracted from the ISO into `game/` with a small XDVDFS extractor (XGD3, 4,065 files, 7.6 GB).
- Measuring: `tools/deck/grid.sh` (frozen grid) and `tools/deck/bench.sh` (AI race, 60 s), 1280x800, MSAA 1x,
  uncapped (FM2_MAX_FPS=1000), **runs alternated** (A B A B). Run-to-run noise ~±3%, and the machine drifted ~5% over
  the afternoon (only alternated pairs are compared).

## The Deck's limit: power

The APU's 15 W are shared by CPU and GPU. With the CPU busy (3.2 GHz), the GPU ran at **600-800 MHz** of its 1,600
while 99-100% busy: the game was GPU-bound *because of* CPU work. Every CPU saving came back as GPU clock (the same
finding as the FM3 Deck work). So CPU work per frame matters on the Deck even where the GPU looks like the limit.

## Results

AI benchmark race, 1280x800, MSAA 1x, uncapped (alternated pairs):

| | avg fps | 1% low | 0.1% low | frames > 20 ms | GPU clock |
|---|---|---|---|---|---|
| package `fm4.exe` (0) | 66.3 / 69.7 | 37.2 / 35.3 | 27.7 / 21.4 | 197 / 112 | 657 MHz |
| + changes 1-5 below | 79.0 / 80.5 | 42.2 / 45.0 | 28.4 / 31.9 | 43 / 33 | 716-724 MHz |
| + PGO (6) (vs 1-5 in the same hour) | 86.3 / 87.5 vs 85.1 / 79.7 | 45-47 vs 44-46 | | 23-24 vs 29-32 | 838-848 vs 720-725 MHz |

Frozen grid: package 61.8-61.9 fps -> 81-82 fps with everything (+32%).

**At the OLED's 90 Hz** (FM2_MAX_FPS=90, the Deck profile's cap; one race each): the Deck build holds **90.0 fps**
average (median 10.7 ms) against the package's **70.3** (median 13.8 ms), frames > 20 ms 33 against 97, GPU 866
against 623 MHz, at the same power (whole run 14.4 against 14.2 W). The 1% low (44.7 against 39.4 fps) is the next
target: the remaining long frames (20-46 ms, clustered at one moment of the race) are a mix of the game's own frame
taking long (draw thread idle), GPU spikes, and dozens of objects created at once.

**Picture:** the texture watch is pixel-identical (grid, the same frozen frame, watch on / off / on live: 0 of 921,600
pixels differ). The other changes don't touch the picture (waits, input, bit-exact audio, PGO = the same code laid
out differently; the MSAA layouts only with MSAA on). Screenshots between different builds differ by the freeze
instant (the grid freezes at a slightly different moment at different frame rates: crowd, flags), as on FM3.

## The changes (all behind switches; the general ones are on by default)

General (every machine where it applies; nothing here is Deck-only):

1. **Texture write watch** (ReGPU `renderer.cpp`, GetTextureLocked): textures use the port's write watch
   (Host::watchArm/watchClean, as index buffers and the vertex arena already did). A texture none of whose pages was
   written since its last hash is unchanged without being read, so it is checked every frame for nothing. Before,
   every texture check hashed (most ahead, on three prehash worker threads: ~0.5 of a core on the Deck's grid, every
   frame). Changing textures are hashed for 120 frames before being watched again; ones whose pages are written with
   the same contents twice are left alone for 1200 frames (the index watch's rules). `REGPU_TEXTURE_NO_WATCH=1` (live)
   = before; `REGPU_TEXTURE_WATCH_VERIFY=1` hashes anyway and logs changes the watch missed: **0** on the grid, ~7
   write faults a second. Grid, live A/B in one boot: **62.5 / 63.0 -> 71.2 / 71.9 fps (+14%)**. Side effect: textures
   that are watched are checked every frame, so the stagger's delay (a streamed texture showing late) is gone for them.
2. **Wine waits on by default under Wine** (ReGPU `wine_wait.h`, from the FM3 Deck work, cherry-picked: 8115e58,
   d2ac29c): short sleeps and timed waits (vblank ticks, command processor idle, frame limiter, FM4's ring wait) are
   NtDelayExecution / NtWaitForSingleObject instead of high-resolution waitable timers, which under Wine are
   wineserver objects (a server round trip per wait). Never on outside Wine; `REGPU_WINE_WAITS=0` = before.
3. **XMA codec reuse on by default** (FM4 `audio/xma.cpp`, from the FM3 Deck work): a retired FFmpeg XMA2 codec is
   reset in place instead of freed and reopened (~8 MB of zeroed allocations per stream restart, ~150 a second in a
   race; the pool waited thousands of times per 5 s in the package build, 0 now). **Bit-exact**:
   `FM2_XMA_REUSE_VERIFY=1` shadowed 2,887 reused codecs over 15,000 packets of a race, 0 differ. `FM2_XMA_REUSE=0` =
   before.
4. **MSAA surface layouts by driver** (ReGPU `renderer.cpp`): on RADV (Mesa) multisampled colour surfaces stay in the
   optimal layouts (RADV resolves them without a decompression, and can't fast-clear in GENERAL: FM3 Deck race +4.3%);
   every other driver keeps GENERAL (AMD's Windows driver needs it). `REGPU_VK_NO_MSAA_GENERAL=0/1` forces either. FM4
   grid at 2x MSAA, alternated boots: GPU 12.40 / 12.45 ms against 12.63 / 12.86 ms in GENERAL (fps 75.8 / 75.9 against
   75.9 / 73.8: the draw thread, at 93%, is the limit there).
5. **DirectInput only for wheels** (FM4 `input/wheel/wheel_system.cpp`): with `wheel_mode = off` DirectInput isn't
   created at all, and in auto mode gamepad-type devices aren't opened (they play through XInput and were never wheels
   there; `wheel_mode = on` still opens them). Before, the Deck's controller and Steam's virtual pad (Valve's VID, so
   not caught by the 0x045E check) were opened and acquired even with the mode off, and Wine's DirectInput thread took
   ~2,700 reports a second. The DirectInput worker is gone; wineserver 23% -> 20% of a core.
6. **PGO** (FM4 `CMakeLists.txt` FM4_PGO=off|gen|use, `src/pgo.cpp`): ReGPU and FM4's own code (not the recompiled
   game code) built with a profile recorded on the Deck (a race + the grid): `pgo/fm4.profdata`. +5.5% in alternated
   races, and the GPU clock ~120 MHz higher (less CPU power). Recording: `LLVM_PROFILE_FILE=...\x-%p.profraw`, play,
   debug server `env FM4_PGO_WRITE 1` (use a distinct file name per run: Wine reuses the pid), llvm-profdata merge.
7. **Proton's xalia off** (`play.sh`: `PROTON_USE_XALIA=0` unless set): a UI-automation helper Proton starts for
   controller navigation of windows; 1.7% of the race's CPU samples, not needed by the game.

Deck preset (`src/deck.cpp`, settings.ini `deck=auto|on|off`, `FM4_DECK=0|1`; auto = under Wine on a Deck APU):

8. **Frame cap = the screen's refresh rate** (60 LCD, 90 OLED) when settings.ini asks for more and FM2_MAX_FPS isn't
   set (memory only). Unseen frames cost the seen ones GPU clock. (In FM4 the cap is the physics rate too: only ever
   lowered, never below 60.)

## Deck presets (recommended settings, from these measurements)

MSAA 2x costs the grid 82 -> 76 fps (Deck build); at 1x the race holds a 90 cap. So, with the Deck build:

| Deck | resolution | MSAA | frame cap | why |
|---|---|---|---|---|
| OLED (90 Hz) | 1280x800 | 1x | 90 (the profile's cap) | the race holds 90.0 avg at 1x |
| OLED, smoother edges | 1280x800 | 2x | 60 | ~76 on the grid at 2x: 60 with headroom |
| LCD (60 Hz) | 1280x800 | 2x | 60 (the profile's cap) | the same headroom at 60 |

FSR 1 (`FM2_UPSCALER=fsr1`, quality 2: the scene at 59%), alternated grid boots: 80.9 / 79.9 against 78.8 / 78.8 fps
native (+2%), GPU time the same: the limit is vertex work and the draw thread, which upscaling doesn't reduce, so it
buys almost nothing for a softer picture. Not in the presets. Not done: the presets as launcher choices.

## Measured, not kept

- **A lighter interrupt lock** (the six `std::recursive_mutex` interrupt locks as an atomic owner + WaitOnAddress):
  in theory Wine's SRW release always calls RtlWakeAddressAll (ntdll's address wakes were ~9% of the race's CPU
  samples, 84% of the guest vblank thread), but alternated races were **4% slower** (75.8 / 76.7 against 79.1 / 81.0).
  Reverted. Where those wakes come from is still open: a frame-pointer build would show the callers.
- `REGPU_NO_ANISO`: 82.0 against 82.0 fps (live): no cost to keep anisotropic filtering.
- `REGPU_VS_SPECIALIZE`: 82.2 -> 73.5 fps (live, once its variants were built; 26 fps while building). Loses here too.
- `REGPU_SPIRV_FINITE_MUL` (vertex code -17%), after a warm-up boot, alternated grid boots: 81.8 / 78.9 against
  80.8 / 79.8 fps, GPU time the same (11.7 / 11.6 against 11.5 / 11.8 ms). No gain: the vertex cost isn't the
  arithmetic it removes.

## Where the time goes now (for the next steps)

GPU split on the grid (`REGPU_EXP_DRAW`, live): normal 11.3-11.5 ms; no draw calls 2.1 ms; one triangle a draw 6.7 ms;
all vertices with a 1x1 scissor **10.6 ms**. **The Deck's GPU is bound by the translated vertex shaders** (~8.5 ms of
vertex work; pixel shading ~0.8 ms), unlike the 7900 XTX. Bandwidth isn't the issue at 1x (aniso free).

CPU (race profile, `tools/deck/fm4profile.sh`, perf as the game's ancestor, names from the PDB): guest 3 (game main)
15.7%, the draw thread 13.5% (DrawImpl 11.5%, memmove 7.7%, FullHashAvx2 7.0%, GetPipeline 3.1%, index arena map
2.8%), guest 7 9.8% (a quarter of it ntdll address wakes), guest 13 8.3%, guest 17 6.0%, command processor 5.6%
(memmove 18.6%, LogRegs 12.8%), replay 5.1% (RADV), guest vblank 5.0% (84% address wakes), wineserver 2.4%.

## Recommended next (in order)

1. Vertex shader cost on small GPUs: the translated vertex shaders (median ~1,300 instructions) are the Deck's GPU
   limit, and FINITE_MUL's arithmetic cut didn't help: look at RADV's statistics for them (`RADV_DEBUG=shaderstats`:
   VGPRs, spills, waves; fetch and export count) before changing code. VS_SPECIALIZE lost on pipeline switches:
   specialise fewer words, or keep its variants in the manifest.
2. Find the address-wake callers (Wine's RtlWakeAddressAll at every SRW release): a build with
   `-fno-omit-frame-pointer` and `perf --call-graph fp`.
3. The command processor's register log (memmove + LogRegs ~30% of it) and the draw thread's FullHashAvx2.
4. The wineserver's remaining ~20%: Wine's XInput thread reads the Steam virtual pad ~2,000 times a second.

## Notes

- A plain `./play.sh` (or `play-deck.sh`) started from a script here, with no window focus, sat at 1 fps after the
  first seconds: the package's exe the same as the Deck build, so not from these changes (the scripted test boots
  don't show it). Worth checking in Game Mode.
- The disc's `$SystemUpdate` folder was extracted into `game/` too (harmless).

## Files

- Code: this branch (`deck`, from `regpu-vulkan` 464782b). The ReGPU side (texture write watch, MSAA layouts by
  driver, Wine waits, plus the cherry-picked FM3 Deck commits 8115e58, d2ac29c, 93dd3fd) is
  `docs/deck/regpu.patch`, against ReGPU `vulkan` a8abaa9: apply it to the ReGPU in `FM4_REGPU_DIR` before building
  (`src/hooks/fm4/ring_wait.cpp` includes its `regpu/wine_wait.h`).
- PGO profile: `pgo/fm4.profdata` (recorded on the Deck for this exact source; configure with `FM4_PGO=use`).
- Deck tools: `tools/deck/` (threads.py per-thread CPU, fm4profile.sh/.py perf profiles, abgrid.sh/abrace.sh
  alternated A/B, grid.sh/bench.sh fixed for Linux, sensors.sh, play-deck.sh = play.sh with xalia off starting
  `deck-build/fm4.exe`). The A/B scripts carry the Deck's local paths at the top: adjust them for another machine.
