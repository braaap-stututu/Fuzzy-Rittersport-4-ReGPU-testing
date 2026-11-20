#pragma once
#include <cstdint>

// The main loop's frame time (src/hooks/fm4/frame_time.cpp). FM4_FREEZE_TIME=1 stops the world (main-loop dt 0)
// while every frame is still drawn; the test-grid route (race_start.cpp) freezes through FrameTimeFreeze.
void FrameTimeFreeze(bool on);
// Main-loop frames counted so far.
uint64_t FrameTimeMainFrames();
