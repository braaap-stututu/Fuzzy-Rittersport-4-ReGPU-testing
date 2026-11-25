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

(22 Nov; measured with the test scripts' 1000 Hz vblank. Corrected and partly built in on 25 Nov: see "Locks, the draw
thread, frame pacing and presets" below.)

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

1. ~~Vertex shader cost~~: measured 24-25 Nov (section "Vertex shaders on the Deck" below): half of it was the
   fixed cost of draws and pipeline binds; what's left is the guest's maths. Next there: fewer pipeline binds.
2. Find the address-wake callers (Wine's RtlWakeAddressAll at every SRW release): a build with
   `-fno-omit-frame-pointer` and `perf --call-graph fp`.
3. The command processor's register log (memmove + LogRegs ~30% of it) and the draw thread's FullHashAvx2.
4. The wineserver's remaining ~20%: Wine's XInput thread reads the Steam virtual pad ~2,000 times a second.

(25 Nov: items 1-4 measured, no lever found that moves the frame rate; see "Locks, the draw thread, frame pacing and
presets". What's left: the GPU's per-draw cost is the game's draw order; the draw thread's hashing of small vertex
buffers could only go with a write watch on them (page faults at Wine's cost when they are rewritten); the 1% lows
under the cap are mostly a missed vblank tick, and the default tick rate measured best.)

## Vertex shaders on the Deck (24-25 Nov 2026)

The first open item ("the Deck's GPU is bound by the translated vertex shaders, ~8.5 ms") measured again, with tools
that show what the driver compiled. Short version: about half of that 8.5 ms was the fixed cost of each draw and
pipeline switch, not vertex work, and the vertex shaders' cost isn't memory latency or a RADV setting.

**New diagnostics (ReGPU, all off unless asked for):**
- `REGPU_SHADER_STATS=1` now also uses VK_KHR_pipeline_executable_properties (RADV has no VK_AMD_shader_info under
  Proton): one log line per pipeline stage with every statistic the driver gives: VGPRs, SGPRs, spills, waves per
  SIMD, instructions, VALU / SALU / VMEM / SMEM counts, ACO's latency and throughput estimates.
- `REGPU_SHADER_ISA=<vs>/,<vs>/,...` (with SHADER_STATS): NIR, ACO IR and the final assembly of those pipelines to
  `out/isa_<vs>_<ps>_<stage>.txt`.
- The debug server's `drawstats` now gives each shader pair's index count too, and `seq`: draws issued, draws right
  after a pipeline bind, of those with the vertex shader changed / only the pixel shader changed.
Measure with a cold pipeline cache: move `vk-pipelines-1002-1435.bin` aside (keep a copy) and set
`MESA_SHADER_CACHE_DISABLE=true`, or the driver hands back cached pipelines without statistics.

**What the frozen grid draws** (one frame, 1280x800, MSAA 1x): 3,023 draws, 82 vertex shaders, 2.05 M indices. The
top 10 vertex shaders take 72% of the indices (the top one 17%). RADV's view of them: 32-96 VGPRs, **no spills, no
scratch**, 10-32 waves per SIMD (occupancy is fine), 1,100-4,300 instructions (index-weighted 2,211).

**What they spend it on** (the top shader's assembly, 1,559 instructions): 903 VALU, of which only about 250 are the
guest's own maths (legacy multiplies and adds, kept unfused by `precise`); 198 `v_cndmask`. Each guest word read by
a vertex fetch was its own divergent bounds-check branch, its own gmem/gphys branch (with a descriptor load), the
load, a full wait (`s_waitcnt vmcnt(0)`), then an endian swap built in all three forms and picked by three selects.
18 full waits for 24 loads: on paper the shaders wait on memory one load at a time (ACO's latency estimate 18,330
cycles a wave).

**The GPU frame split** (`REGPU_EXP_DRAW`, live, two rounds alternated in one boot; GPU ms per frame):

| mode | what is drawn | GPU ms |
|---|---|---|
| 0 | normal | 12.0 / 12.0 |
| 1 | no draw calls (all state still set) | 2.76 / 2.69 |
| 2 | one triangle per draw | 6.57 / 7.16 |
| 4 | one triangle, drawn twice | 8.68 / 8.88 |
| 5 | one triangle, only draws with no pipeline bind before them (1,626) | 4.50 / 4.64 |
| 6 | one triangle, only draws right after a pipeline bind (1,397) | 6.11 / 6.54 |
| 7 | as 6, only where the vertex shader changed (973) | 5.70 / 5.18 |
| 8 | as 6, only where just the pixel shader changed (397) | 3.95 / 3.72 |
| 3 | every vertex, 1x1 scissor (no pixels) | 10.98 / 10.83 |

So per frame: ~2.7 ms without draws, **~4.2 ms of fixed per-draw cost**, ~4.0 ms of vertex work, ~1.1 ms of pixels.
Per draw: a bare repeat of the same draw costs the GPU 0.63 us, a draw with its state changes but no pipeline bind
1.14 us, a draw after a pipeline bind ~2.7 us. That's the same whether the vertex shader or only the pixel shader
changed (2.8 / 2.8 us), so it's the bind, not the shader code. ReGPU's bind is a plain vkCmdBindPipeline, and dynamic
blend (which would make RADV build pixel-shader epilogs) is already opt-in. Binds are ~3.7 ms of the 12.

**Tried, no gain (alternated boots, GPU ms per frame):**
- **Batched vertex fetch** (`REGPU_VFETCH_BATCH=1`, ReGPU spirv/translate.cpp, now opt-in): one uniform buffer branch
  per fetch, clamped addresses with the past-the-end word zeroed after the load, a two-step swap. RADV's estimate
  fell (index-weighted instructions 2,211 -> 1,944, latency 28,363 -> 21,996 cycles), the loads now issue back to back
  with one wait. The frame didn't move: grid 12.43 / 12.12 / 12.27 against 11.92 / 12.43 / 13.20 ms without it, and
  every vertex with no pixels (EXP_DRAW 3) 11.20 / 10.90 / 11.54 against 11.53 / 11.49 / 11.00 ms. Enough waves hide
  the latency; the estimate overstated it.
- **RADV_DEBUG=nonggc** (no culling in the vertex shader): 12.12 / 12.45 / 12.23 against 12.37 / 12.12 / 12.46 ms.
- **RADV_PERFTEST=gewave32** (32-wide vertex waves): 12.29 / 12.18 / 12.34 ms, the same.
- Today's build against the 22 Nov Deck build: 12.34 / 12.41 / 12.61 against 12.52 / 12.45 / 12.26 ms, the same fps
  (the new diagnostics cost nothing). Both ran at ~76 fps where the same exe made 81-82 on 22 Nov: drift.

Also: on the grid the draw thread is 91-94% busy at a 13 ms frame, so the CPU side is about as close to the limit as
the GPU there.

**Next for the GPU** (in order): fewer pipeline binds (the 1,397 a frame are the game's draw order between shaders,
so this is about binds that change nothing the driver needs, and pipelines that could be merged), and what RADV
emits per bind (RADV_DEBUG=... or an RGP capture of the grid would show whether it's context rolls or something
heavier). The vertex work itself (~4 ms) is the guest's maths plus the fetch decode; no cheap lever found.

## Locks, the draw thread, frame pacing and presets (25 Nov 2026)

The open items of the vertex-shader section, worked through in order. Short version: none of the CPU or GPU levers
measured here moves the frame rate (the Deck's CPU and GPU are both at the limit of the APU's 15 W, so work saved on
one side alone barely shows); the real-play frame pacing under the cap is good, and the test scripts' fixed 1000 Hz
vblank was misleading for capped runs; the Deck profile now has presets for what settings.ini doesn't set.

**GPU binds.** Of the grid's 1,397 pipeline binds a frame, 1,370 change a shader (973 the vertex shader, 397 only the
pixel shader): they follow the game's draw order, so there are no redundant binds to drop. `RADV_DEBUG=nobinning`
(binning flushes at state changes), alternated grid boots: 78.8 / 76.9 / 76.8 against 79.8 / 77.9 / 77.9 fps, GPU
time the same: no gain. The draw thread was 92-98% busy in those boots: on the grid the GPU and the draw thread are
both at ~12 ms.

**Address wakes (Wine's SRW release).** Counted by call site (new diagnostic `FM4_LOCK_STATS=1`, src/lock_stats.cpp:
wraps fm4.exe's lock imports, samples stacks; perf can't walk Win64 stacks, frame pointers or not): in the AI race
~660,000 exclusive and ~250,000 shared SRW releases a second plus ~390,000 WakeByAddressSingle. Callers: the guest
critical sections (RtlLeaveCriticalSection woke at every leave), the six interrupt locks (PPCMsrChanged), ReGPU's
texture mutex at every per-frame cache miss of the draw thread (GetIndexBuffer, GetTexture, BindlessIndex,
VertexCache) and the write watch's shared lock (WriteWatchClean, ~120,000 a second). A futex-style mutex that wakes
only waiters, a conditional critical-section wake and a lock-free WriteWatchClean took that to ~107,000 calls a
second, but six alternated races: 91.7 against 92.9 fps (+1.2%, inside the noise), and in a fresh profile all of
ntdll is 4.5% of the samples, its lock and wake functions ~3% (the "9%" of 22 Nov was something else). **Reverted**:
no measured gain for the risk.

**Draw thread** (frozen grid, sampled with stacks: FM4's thread sampler, now with `FM2_SAMPLE_RAW=1` for offline
names and a guarded unwind, see below): DrawImpl 18%, FullHashAvx2 7.4% (5.5% UploadGuest: the small vertex buffers
under 16 KB hashed at every use, 7.7 MB a frame over 1,612 uploads, 1,132 of them reuses; 1.9% CachedIndexList),
memcpy 7.3% (shader loads, DrawImpl, the work loop, uploads), GetIndexBuffer 5.2%, GetPipeline 5.1%. The existing
opt-in `REGPU_UPLOAD_STABLE_REUSE` (sampled check for long-stable ranges) live on the grid: 82.1 against 82.3 fps.
Not changed. Command processor (LogRegs, memmove) is ~0.7% of the CPU and not the limit: not changed.

**XInput.** Wine's XInput thread is 2.6% of a core and the whole wineserver ~12% at a cap: driven by the Steam virtual
pad's report rate (the thread reads HID reports), not by the game's XInputGetState calls. Nothing to change on the game's side.

**Frame pacing under the cap** (the Deck profile always caps). `tools/deck/bench.sh` fixes the vblank at 1000 Hz
(`REGPU_VBLANK_HZ=1000`, for the uncapped tests); real play ticks at twice the cap. `BENCH_VBLANK_HZ=real` now gives
the real rate, `BENCH_MAX_FPS` a cap, and `tools/deck/capab.sh` alternates capped races with CPU, GPU and APU power
over the same 40 s. AI race, 1280x800, MSAA 1x:

| cap | vblank | avg fps | 1% low | p99 | frames > 20 ms | CPU (% of a core) | GPU | APU |
|---|---|---|---|---|---|---|---|---|
| 60 | 1000 Hz (old test setup) | 59.9 | 34-36 | 25.1 ms | 156 of 3,597 | ~420 | 560 MHz | 14.5 W |
| 60 | 120 Hz (real) | 60.0 / 60.0 | 47.7 / 50.9 | 18.0 / 17.4 ms | 13 / 7 | 324 / 311 | 864 / 907 MHz | 14.8 / 14.6 W |
| 60 | 240 Hz | 60.0 / 59.9 | 38.8 / 37.9 | 21.2 / 21.2 ms | 58 / 65 | 361 / 348 | 890 / 829 MHz | 15.8 / 15.5 W |
| 90 | 180 Hz (real) | 84.5 / 85.6 / 86.2 / 85.3 | 49.5-54.6 | 16.6-16.9 ms | 8-18 | 377-416 | ~1,000 MHz | 14.4-15.0 W |
| 90 | 360 Hz | 82.3 / 83.2 | 48.5 / 48.3 | 17.8 / 17.6 ms | 16 / 20 | 462 / 451 | 976 / 960 MHz | 14.7 / 14.5 W |

So the default (vblank at twice the cap) is the best of these: a faster tick costs CPU power, which the 15 W budget
takes from the GPU. At 60, 95% of frames land at 16-18 ms; the slow ones are mostly a missed tick (+8.3 ms). At the
OLED's 90 the race averages ~85 (not the 90.0 measured with the 1000 Hz vblank), p99 under 17 ms. The earlier capped
numbers in this file came from the 1000 Hz setup.

**MSAA 2x at a 60 cap** (1000 Hz vblank run): 59.7 avg, 1% low 33.4 fps, GPU 94% busy: 2x doesn't hold 60 in the race.
The preset table above is corrected below.

**Presets (new, Deck profile).** With no settings.ini (a first start), FM4's built-in defaults were 1280x720 and a
60 cap: bars on the Deck's 1280x800 panel and the OLED's 90 Hz unused. src/deck.cpp now fills in what settings.ini
doesn't set (settings.cpp records the keys it read, `Settings::fromFile`): resolution 1280x800, frame cap = the
screen's rate (90 OLED, 60 LCD). MSAA stays 1x. `FM4_DECK_PRESETS=0` turns them off; FM2_RESOLUTION /
FM2_RENDER_SCALE / FM2_MAX_FPS still win. Checked on the OLED: "deck: preset resolution 1280x720 -> 1280x800",
"preset frame cap 60 -> 90". Recommended (replaces the table above): OLED 1280x800 1x at 90 (~85 in the race, smooth)
or at 60 (locked); LCD 1280x800 1x at 60. 2x only where 60 with drops is acceptable.

**Diagnostics added** (all off unless set): `FM4_LOCK_STATS=1`; `FM4_FRAME_POINTERS` CMake option (`-mframe-pointer=all`;
note: Win64 frame pointers point into the frame, not at the saved rbp, so perf still can't walk them);
`FM2_SAMPLE_RAW=1` with `FM2_SAMPLE_THREAD=w FM2_SAMPLE_CALLERS=1` (every sample as fm4.exe offsets for
llvm-symbolizer: dbghelp under Wine names few functions of the clang PDB; the unwind now stays inside fm4.exe and is
guarded: it faulted in ntdll's Unix side and took the game down).

## Notes

- A plain `./play.sh` (or `play-deck.sh`) started from a script here, with no window focus, sat at 1 fps after the
  first seconds: the package's exe the same as the Deck build, so not from these changes (the scripted test boots
  don't show it). Worth checking in Game Mode.
- The disc's `$SystemUpdate` folder was extracted into `game/` too (harmless).

## Files

- Code: `deck-work/fm4.patch` (FM4 against 464782b) and `deck-work/regpu.patch` (ReGPU against a8abaa9, includes the
  cherry-picked FM3 Deck commits). Not committed anywhere (the user commits).
- Deck build: `deck-build/` (fm4.exe + fm4.pdb, PGO, the Deck profile); `deck-build/play-deck.sh` starts it.
- Deck tools: `tools/deck/` (threads.py per-thread CPU, fm4profile.sh/.py perf profiles, abgrid.sh/abrace.sh
  alternated A/B, grid.sh/bench.sh fixed for Linux).
