# Performance test tools

An unattended, repeatable scene for A/B tests: FM4 frozen on a race grid (src/hooks/fm4/race_start.cpp,
frame_time.cpp), driven through ReGPU's debug server on port 7351.

| Tool | Use |
|---|---|
| `boot-min.ps1 -Log out\x.log -Extra @{ NAME='value' }` | Starts `build\fm4\fm4.exe game` minimized (not activated) with the test settings: debug server on 7351, `REGPU_VBLANK_HZ=1000` (test-only frame-rate unlock), `FM4_GRID_TEST=1`. Add `FM4_WHEEL_MODE='off'` to leave a wheel alone, `FM2_RESOLUTION='1280x720'; FM2_MSAA='1'` for a CPU-bound case, `REGPU_GPU_PASSES='1'` for GPU time per pass. The log is `out\x.log.err`. |
| `route.py out/x.log.err [timeout]` | Presses A until the log says `grid test: frozen`: Career, Go Race, World Tour, its first event, Start Race (a fresh profile's World Tour; ~75 s). |
| `ab.py fps [s]`, `ab.py ab REGPU_NAME [rounds] [s] [switch\|env]` | Frame rate from the swap counter; interleaved off/on rounds of a live switch with medians. Compare within one boot (boot-to-boot noise is several percent). |
| `sample.py <pid> <tid\|busiest\|topN> <s> build/fm4/fm4.exe [N]` | Sampling profiler (suspend, read RIP, resume) symbolized with llvm-symbolizer and the PDB; for samples outside the exe, the exe return addresses found on the stack (heuristic). |
| `../pngdiff.py a.png b.png [threshold]` | Pixel difference of two screenshots (`regpuctl.py shot out/a.png`). |

Debug-server commands while frozen: `env FM4_FREEZE_TIME` lets the race run, `env FM4_FREEZE_TIME 1` freezes it again,
`switch REGPU_<NAME> 0|1` flips a live switch, `quit` closes the game (`py -3 thirdparty/ReGPU/tools/regpuctl.py`,
with `REGPU_DEBUG_PORT=7351`). Measurement-only switches: `REGPU_EXP_ONE_TRIANGLE`, `REGPU_EXP_TINY_SCISSOR`.
