@echo off
rem Test grid: FM4 at your launcher settings, the frame-rate lock lifted for testing (REGPU_VBLANK_HZ=1000: movies
rem and other vblank counters run fast, so this is not for playing), and the world frozen on the start grid of the
rem next race's countdown (FM4_GRID_TEST=1, src\hooks\fm4\race_start.cpp; FM4_GRID_FREEZE_AT=<seconds>, default 9).
rem The race keeps drawing the same frame: screenshots are steady and switches can be A/B'd live through the debug
rem server on port 7351 (py -3 thirdparty\ReGPU\tools\regpuctl.py with REGPU_DEBUG_PORT=7351;
rem "env FM4_FREEZE_TIME" lets the race go on, "env FM4_FREEZE_TIME 1" stops it again).
rem Route to the race: tools\fm4-route.txt (one line of FM2_TOUR steps) if it exists, else drive the menus yourself;
rem your input is recorded to out\route-input.txt either way, to make that file from.
rem Log: out\play-fm4-grid.log.
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
set FM4_GRID_TEST=1
set FM2_RECORD_INPUT=out\route-input.txt
if exist tools\fm4-route.txt set /p FM2_TOUR=<tools\fm4-route.txt
build\fm4\fm4.exe game > out\play-fm4-grid.log 2>&1
