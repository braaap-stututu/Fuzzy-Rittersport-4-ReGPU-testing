# FM4 on the Vulkan ReGPU (branch `regpu-vulkan`)

Started 21 Nov 2026. This branch builds FM4 against braaap-stututu/ReGPU, branch `vulkan`: the Vulkan-only ReGPU the
FM3 port moved to. `fm4-perf` keeps the vendored D3D12 copy in `thirdparty/ReGPU`.

## Build

- `FM4_REGPU_DIR=<ReGPU checkout>`: the ReGPU to build. The default is the vendored D3D12 copy.
- `FM4_DXC_DIR=<folder with dxc.exe, dxcompiler.dll, dxil.dll>`: a DXC with SPIR-V output (a DirectXShaderCompiler
  release, or the Vulkan SDK's). The default is `$VULKAN_SDK/Bin`.
- `thirdparty/fidelityfx` must be the FidelityFX SDK **2.4.0** or newer. 2.1.0 has no Vulkan FSR DLLs; without them
  FSR 3/4 fall back to FSR 1.
- `-mcx16` is set for the game and ReGPU. MSVC's `<atomic>` marks its functions `target("cx16")`, so without the flag
  clang-cl calls every `std::atomic` access and fence instead of inlining it.

Development build on the user's PC (`ReGPU-V\work\scripts\buildfm4vk.bat`): the worktree `ReGPU-V\work\fm4vk` is built
in `ReGPU-V\work\fm4vk-build`, with junctions to `target\`'s XenonRecomp, `out\ppc*`, ffmpeg and game data.

## Playing it (the user's PC)

`FM4 Launcher (Vulkan).cmd`, in the workspace folder beside `FM4 Launcher.lnk`, starts the same launcher with
`FM4_EXE` set to `target\build\fm4-vulkan\fm4.exe`. The launcher runs `FM4_EXE` when it is set (`Install.cs`,
`Paths.Exe`). That folder is a copy of the development build, refreshed by hand after a rebuild.

Saves and settings are the same (`Documents\fm4`). The Vulkan shader caches don't clash with D3D12's:
`.vk1.spv` and `vk-pipelines-*.bin` against `.dxbc` and `pipelines.bin`; the `.ucode` files are the same microcode.
The shortcut starts the D3D12 build as before.

## What FM4 needed on Vulkan

- `REGPU_TEXTURE_GAMMA=1`, set in `gpu_host.cpp`. Gamma-signed texture components are converted to linear on fetch,
  as the console does. The Vulkan ReGPU has this opt-in; without it the picture is washed out and too bright (grid
  mean 121 against D3D12's 92, now 90.7). `FM4_NO_TEXTURE_GAMMA=1` turns it off.
- The soft car-shadow cascade switches (`REGPU_DEPTH_AS_8888`, `REGPU_CASCADE_SLOTS`, `REGPU_CASCADE_B_LIT`), ported to
  the Vulkan ReGPU in `0e2cfdd`. They are packed into `tinfo.z` bits 16-23, in the HLSL preamble and the SPIR-V
  translator.
- API: `regpu::DecodeTexture`, `WritePng`, `CompileIdle` and `BenchmarkRecord`/`BenchmarkWriteReport` are now public
  in ReGPU (`cee03cf`). `regpu::LogCrashContext` replaces `RendererLogCrashContext`. `LogStart`/`LogSeconds` were added
  to `src/log.cpp`. `smaa=N` maps to ReGPU's SMAA post-filter.
- Still from FM4's ReGPU, not ported: `REGPU_COLOR_SURFACE_PER_BPP` (photo mode's 64-bit accumulation) and the
  other features in ReGPU's `docs/fm4-features-to-port.md`. Photo mode is untested.

## Speed (1440p 4x MSAA, RX 7900 XTX + 9800X3D)

FM3's ReGPU speed defaults are set in `gpu_host.cpp` (`FM4_NO_FM3_DEFAULTS=1` turns them off): the draw thread
(serial), asynchronous present, the vertex arena, lean fetches, fetch variants, lazy loops and direct dynamic indices.
Without the draw thread, FM4 ran every draw on the command processor.

ReGPU changes made for FM4 (all of them help every port):

- **Resolve write-backs from half-float images:**
  - F16C conversion, column spans worked out once, SSE packing, whole-word swaps (`aae02a0`).
  - Large writes in 32-row bands on the thread pool (`37c283c`).
  - FM4 writes its 512x512 road image back every frame; the old scalar path was about 40% of the draw thread.
  - `REGPU_WRITEBACK_VERIFY=1` compares every write with the scalar path byte for byte (86,016 writes, none differ).
  - `REGPU_SLOW_WRITEBACK=1` and `REGPU_SERIAL_WRITEBACK=1` give the old behaviour.

Frozen grid: 144 fps (launcher switches only) -> 171 (FM3 defaults) -> 241 (fast write-backs). The GPU is now the
limit there (4.1 ms).

AI benchmark race, measured minimized (frame times at the draw thread's frame end, `REGPU_FRAME_TIMES_SWAP`):

| | fps | 1% low | 0.1% low |
|---|---|---|---|
| FM3 defaults, old write-backs | 161.7 | 92.4 | 75.4 |
| fast write-backs | 234.8 | 138.6 | 84.2 |
| + parallel bands (4 runs) | ~238 | ~166 | ~139 |

FM4's own D3D12 build (`fm4-perf`) runs the same race at 158.6 fps (visible window, debug-server fps).

Game side: `hooks/fm4/ring_wait.cpp` (FM3's ring wait) makes the render thread sleep ~100 us between D3D GPU-wait
rounds (`sub_826EDE50`) instead of spinning. Frozen grid, live: guest 7 drops from 95% to 48% of a core and the game
from 3.9-4.4 to 3.6-3.8 cores, with fps the same. `FM4_RING_WAIT=0` spins as before. This matters on 4-core CPUs
(the Deck).

Write watch (`memory.cpp`, from FM3; `Host::watchArm`/`watchClean`/`watchSeq` in `gpu_host.cpp`): the pages of vertex
ranges, index buffers and shader loads are made read-only, and the first write to one faults and marks its 64 KB as
written. ReGPU keeps the hash of a range none of whose pages was written since, instead of hashing it again at its first
use in each frame. `NtReadFile` disarms its buffer first (the kernel's write fails on a read-only page instead of
faulting). About 30 faults a second on the grid. `FM4_NO_WRITE_WATCH=1` turns it off; `FM4_WATCH_LOG=1` logs the
faults. 720p grid, boots alternated: +3-9% against the boot after each.

ReGPU `e30f7cb`: a large vertex buffer's "is it changing" answer is now kept per thread for the frame even when it was
not hashed in it. Before, every fetch of a quiet buffer took a mutex and a map lookup (9% of the draw thread at 720p).
720p grid, live A/B: 263 -> 279 fps.

GPU side (22 Nov 2026 night; the user: "optimize the GPU side until it catches up to the CPU side"):

- The CPU ceiling: the same AI race at 720p 1x (the GPU nearly idle) runs 304 fps, 1% low 202. At 1440p 4x the GPU was
  slower than that ceiling in a quarter of the race's seconds (`work\bench\gpuvscpu.py`).
- **`REGPU_FETCH_SPECIALIZE`, now on by default** (`gpu_host.cpp`; `FM4_NO_FETCH_SPECIALIZE=1` off): each pixel shader's
  fetches compiled for the draw's fetch constants. Live in one boot: GPU 3.59 -> 3.10 ms. AI race at the same clock:
  296.4 -> 307.4 fps, 1% low 204 -> 213, 0.1% low 171 -> 182 (the CPU ceiling). It also ends the "slow boots" (the same
  grid at 3.3-3.6 ms instead of 3.1): every boot with it ran 3.11-3.18. Screenshot: 4 of 3.7 M pixels differ, by <= 7/255.
- **`REGPU_VS_SPECIALIZE`, now on by default** (ReGPU `ad8798b`; `FM4_NO_VS_SPECIALIZE=1` off): vertex shaders compiled
  for their draws' wrapper words (viewport transform, index source, points, rectangles, vertex fetch endian and buffer).
  Vertex shaders offline: median 1,441 -> 712 GPU instructions. Live in one boot, alternated: GPU 3.14 -> 3.01 ms;
  screenshots identical.
- Races alternated afterwards were not comparable (other programs were using the PC, 242-269 fps for both).

## Testing (no window in the way)

Scripts in `ReGPU-V\work\bench`:

- `fm4boot.ps1 [-Exe] [-Log] [-User] [-Extra @{}] [-Route]`: boots minimized with the launcher's switches
  (`REGPU_EARLY_FENCES=1`, `REGPU_RESOLVE_WRITEBACK_MAX=1024`), wheel off, and `FM4_USER_DIR` set to a copy of the
  saves, so the shader caches stay apart. `-Route` drives it to the frozen grid.
- `fm4bench.ps1 -Label x [-Seconds 60] [-Extra @{}]`: an AI benchmark race. `FM4_BENCHMARK` with the race AI driving
  the player's car (`hooks/fm4/ai_player.cpp`). It works: the car moves from 6th to 2nd in 30 s. Prints the average,
  p99 and the 1% and 0.1% lows (`ReGPU tools/frametimes.py`).
- `fm4route.py`: presses into the race until a log line appears.
- `resdiff.py`: compares two folders of resolve dumps.
- Profile: `tools/perf/sample.py <pid> topN <s> <exe>`, with `LLVM_SYMBOLIZER` set to the toolchain's.
- `fm4gpu.ps1 -Label x [-Extra @{}]`: the 1440p 4x grid's GPU time for one boot, with the GPU clock.
- `expdraw.py LOG NAME v1 v2 [--switch] [--rounds N]`: sets an env variable (or, `--switch`, a live switch) to each value
  in turn on the running game and prints the GPU time per frame for each: A/B inside one boot.
- `shotab.py NAME OUTDIR`: screenshots with a live switch off and on, compared pixel by pixel.
- `shaderlab\count.py <folder>`: compiles `.spv` files for gfx1100 with the RDTS's amdllpc and counts instructions
  (`regpu_spirv_check --no-dxc --keep <folder> [--vs-spec <vte>] <.ucode files>` writes them).
- `fm4grid.ps1 -Label x [-Extra @{}]`: boots to the 720p 1x grid (CPU-bound), settles, prints the median fps and the
  GPU time per frame. The GPU time of the same frame moves between 2.26 and 3.4 ms from boot to boot, with load on the
  PC from other programs: compare boots only at the same GPU time, or alternate them.
- Always mirror the launcher's switches. Without `REGPU_RESOLVE_WRITEBACK_MAX` the GPU time was 12 ms instead of 4.7.

## Next

- GPU: the race is GPU-bound. The 7900 XTX runs at its board power limit there (~361 W, ~3,050 MHz, 99-100% busy;
  `work\bench\gpusensors.py`, logged by `fm4bench.ps1`). Where the GPU time goes on the 1440p 4x grid (ReGPU
  `REGPU_EXP_DRAW`, live):

  | mode | GPU ms |
  |---|---|
  | normal | 3.10 |
  | 1: no draw calls (state still set) | 1.16 |
  | 2: one triangle a draw | 2.35 |
  | 4: one triangle drawn twice | 2.41 |
  | 3: full vertices, 1x1 scissor | 2.14 |
  | 5: one triangle, only draws with no pipeline switch (1,736) | 1.58 |
  | 6: one triangle, only draws right after a switch (1,176) | 2.22 |

  A draw call costs ~20 ns. A draw right after a pipeline switch costs ~0.9 us against ~0.24 us: the switches are ~0.78 ms
  of the 3.1. Pixel shading is ~1 ms. Switches that change the vertex shader cost more than pixel-shader-only ones.
  The translated shaders are large (`REGPU_SHADER_STATS=1`, 1,184 pipelines; disassembly bytes): vertex shaders median
  77 KB, p90 134 KB; pixel shaders median 85 KB, p90 267 KB, max 1 MB. A sample vertex shader is ~1,300 instructions,
  mostly `v_cndmask`/`v_cmp` (predication as selects), with SGPR spills into VGPR lanes; 114 vertex shaders use scratch.
  The pipelines are monolithic and fully optimised (pipeline libraries are off). Leads: smaller translated shaders
  (branches instead of selects for predicated blocks), or an RGP capture (`work\tools\RadeonDeveloperToolSuite-*`,
  `RadeonDeveloperPanelCLI.exe -m profiling`), read in the RGP GUI.
- Bench noise: on some boots the same frame took the GPU 1.3x longer (clocks or power, not other programs). Compare
  races at the same "GPU while 90%+ busy" clock that `fm4bench.ps1` prints.
- CPU: the game's main thread (guest 3) runs at about 100% in races (game code).
- Write-backs: they are still written on the draw thread at the frame-slot reuse. They could move to the background.
- `REGPU_COPIES_1010102_BIASED=1` (only biased 10-bit copies, never the front buffer) works but gives +0.3%: left off.
- `REGPU_COPIES_1010102` breaks FM4's front buffer (a `k_2_10_10_10` resolve becomes a 10-bit image the presenter
  doesn't take). The presenter would need to accept it.
- Photo mode: `REGPU_COLOR_SURFACE_PER_BPP` is not ported.
