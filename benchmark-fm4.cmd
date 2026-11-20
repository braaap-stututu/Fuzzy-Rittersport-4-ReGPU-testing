@echo off
rem Benchmark race: FM4 at your launcher settings with the frame-rate lock lifted for testing (REGPU_VBLANK_HZ=1000),
rem the race AI driving your car (src\hooks\fm4\ai_player.cpp), measured for FM4_BENCHMARK seconds starting
rem FM4_BENCHMARK_SETTLE seconds after GO (src\hooks\fm4\race_start.cpp); the game then closes itself.
rem Results: the "perf:" lines of out\benchmark-fm4.log between "benchmark: measuring" and "benchmark: done".
rem Route to the race: tools\fm4-route.txt (one line of FM2_TOUR steps) if it exists, else drive the menus yourself.
rem Turn the wheel's force feedback down or off first: the AI drives, the wheel should not move by itself.
cd /d "%~dp0"
if not exist out mkdir out
if not exist build\fm4\fm4.exe (echo build\fm4\fm4.exe not found: build the fm4 target first & exit /b 1)
set FM2_NO_LAUNCHER=1
set FM2_VSYNC=0
set FM2_MAX_FPS=360
set REGPU_EARLY_FENCES=1
set REGPU_RESOLVE_WRITEBACK_MAX=1024
set REGPU_PERF=1
set REGPU_DEBUG=1
set REGPU_DEBUG_PORT=7351
set REGPU_VBLANK_HZ=1000
if "%FM4_BENCHMARK%"=="" set FM4_BENCHMARK=60
if "%FM4_BENCHMARK_SETTLE%"=="" set FM4_BENCHMARK_SETTLE=10
if exist tools\fm4-route.txt set /p FM2_TOUR=<tools\fm4-route.txt
build\fm4\fm4.exe game > out\benchmark-fm4.log 2>&1
