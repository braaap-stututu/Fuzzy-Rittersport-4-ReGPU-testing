// The GPU is the ReGPU library (thirdparty/ReGPU, include/regpu/regpu.h). This file hands it the
// player's display settings and the runtime pieces it needs: guest memory, guest threads, guest
// calls and the interrupt lock. ReGPU provides the Vd* imports itself.
#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <set>
#include <string>

#include "regpu/regpu.h"

#include "boot_screen.h"
#include "pipeline_seed.h"
#include "static_shaders.h"
#include "kernel/runtime.h"
#include "kernel/thread.h"
#include "live_switch.h"
#include "log.h"
#include "memory.h"
#include "settings.h"

std::string CurrentHostStack(int maxFrames); // crash.cpp
void LogAllHostThreadStacks(const char* why); // hang.cpp
void SpikeDump(double ms);                    // kernel/thread.cpp

namespace
{
void ApplySettings(regpu::Config& c)
{
    const Settings& s = GetSettings();
    c.width = s.width;
    c.height = s.height;
    c.aspectW = s.aspectW;
    c.aspectH = s.aspectH;
    c.fullscreen = s.fullscreen;
    c.vsync = s.vsync;
    c.maxFps = s.maxFps;
    c.enhance = s.enhance;
    c.msaa = s.msaa;
    // SMAA (settings smaa=1-4): the Vulkan ReGPU's SMAA post-filter (one quality: the setting's level is not used).
    if (s.smaa)
        c.postAA = regpu::Config::PostAA::Smaa;
    c.upscaler = regpu::Config::Upscaler(uint32_t(s.upscaler));
    c.quality = s.quality;
    c.sharpness = s.sharpness;
}
} // namespace

void GpuStart()
{
    regpu::Config& config = regpu::GetConfig();
    ApplySettings(config);
    config.userDir = UserDir(); // shader and pipeline caches, in the same place as before
    config.windowTitle = L"Forza Motorsport 4";

    regpu::Host& host = regpu::GetHost();
    host.base = g_base;
    host.physicalSection = PhysicalSection();
    // Write watch (memory.cpp, as FM3): ReGPU keeps the hashes of vertex ranges, index buffers and shader loads none of
    // whose pages was written since, instead of hashing them again at every first use in a frame.
    // FM4_NO_WRITE_WATCH=1: none (hashed as before).
    if (!getenv("FM4_NO_WRITE_WATCH"))
    {
        host.watchArm = [](uint32_t offset, uint32_t bytes) { return WriteWatchArm(offset, bytes); };
        host.watchClean = [](uint32_t offset, uint32_t bytes, uint64_t since) { return WriteWatchClean(offset, bytes, since); };
        host.watchSeq = [] { return WriteWatchSeq(); };
    }
    host.alloc = [](bool physical, uint32_t size, uint32_t align) {
        return GuestAlloc(physical ? Heap::Physical : Heap::Virtual, size, align);
    };
    host.createThread = [](const char* name, void (*body)(PPCContext&)) { CreateHostGuestThread(name, body); };
    host.currentPcr = [] { return CurrentGuestThread()->pcr; };
    host.guestCall = [](PPCContext& ctx, uint32_t address) { GuestCall(ctx, address); };
    host.interruptLockAcquire = [](int cpu) { InterruptLockAcquire(cpu); };
    host.interruptLockRelease = [] { InterruptLockRelease(); };
    host.cpuUsage = [](double wallSeconds) { return GuestThreadCpuUsage(wallSeconds); };
    host.logThreadStacks = [](const char* why) { LogAllHostThreadStacks(why); };
    host.currentStack = [](int maxFrames) { return CurrentHostStack(maxFrames); };
    host.slowFrame = [](double ms) { SpikeDump(ms); };
    host.bootScreen = BootScreenCompose;
    host.staticShaders = StaticShaders;
    host.staticShadersSaved = StaticShadersSaved;
    // Then the shipped pipeline list (src/pipeline_seed.h, from data/manifests by tools/pipeline_seed.py).
    host.pipelineSeed = [] { return kPipelineSeed; };
    // The road surface: the game renders its two-channel detail image (k_8_8, each mip level) and the
    // 32x512 strips it builds it from, then reads that memory back as textures in other formats (the
    // strips as DXT1), which ReGPU cannot map onto the rendered images. Without these in memory, every
    // track after the first showed stale strips: streaks along the road while driving. Written back
    // deferred, a frame late, as the image is rebuilt continuously; only the rows each resolve covers (the
    // image is rebuilt half at a time). FM4_NO_ROAD_WRITEBACK=1: not (A/B). FM4_ROAD_WRITEBACK=strips or
    // =image: only that part (test, which one the road needs).
    // Photo mode keeps its 64-bit picture accumulation in EDRAM across frames, with the 32-bit 4x scene
    // drawn over the same base in between: 64-bit colour layouts get their own surface, or the scene (and
    // its purple clear) bled into the photo below the first ~200 rows. FM4_COLOR_SURFACE_SHARED=1: off.
    if (!getenv("FM4_COLOR_SURFACE_SHARED") && !getenv("REGPU_COLOR_SURFACE_PER_BPP"))
        _putenv_s("REGPU_COLOR_SURFACE_PER_BPP", "1");
    // Textures unchanged for a while are checked every 4th/16th frame, not every frame (2.6 -> 0.35 ms a
    // frame of texture checks in a race, no visible change). FM4_TEXTURE_CHECK_EVERY_FRAME=1: every frame.
    if (!getenv("FM4_TEXTURE_CHECK_EVERY_FRAME") && !getenv("REGPU_TEXTURE_CHECK_STAGGER"))
        _putenv_s("REGPU_TEXTURE_CHECK_STAGGER", "1");
    // Gamma-signed texture components (fetch sign mode 3) converted to linear when fetched, as the console does (the
    // Vulkan ReGPU has it opt-in, REGPU_TEXTURE_GAMMA; its D3D12 copy here always did). Without it the colour maps stayed
    // in gamma: the whole picture came out washed out and too bright (grid mean 121 against 92). FM4_NO_TEXTURE_GAMMA=1: off.
    if (!getenv("FM4_NO_TEXTURE_GAMMA") && !getenv("REGPU_TEXTURE_GAMMA"))
        _putenv_s("REGPU_TEXTURE_GAMMA", "1");
    // These write-backs come every frame; a fence held behind each (REGPU's default for a small write-back)
    // tied the game to the GPU every frame (about 60 -> 43 fps). The image is rebuilt continuously, so a
    // copy landing a frame late is harmless: fences are not held. FM4_WRITEBACK_FENCES=1 holds them (A/B).
    if (!getenv("FM4_WRITEBACK_FENCES") && !getenv("REGPU_NO_WRITEBACK_FENCES"))
        _putenv_s("REGPU_NO_WRITEBACK_FENCES", "1");
    // Pixel shaders without the run-time alpha test where a draw has none (early depth works again), and with each
    // draw's texture fetch constants baked in (the fetch code's branches fold): frozen race grid at 1440p 4x on an
    // RX 7900 XTX ~107 -> ~119.5 fps (+12%; +3% and +9% apart), picture identical within 1/255 on 34 pixels.
    // FM4_NO_SHADER_VARIANTS=1: neither (both are also live switches: REGPU_SPLIT_ALPHA_TEST, REGPU_FETCH_SPECIALIZE).
    // The per-draw upload ring (constants, vertex data, index lists) in VRAM through resizable BAR: the GPU stopped waiting
    // on system memory for every draw (about 1.5 us of fixed GPU time a draw before). Frozen grid, RX 7900 XTX: 1440p 4x
    // 119.7 -> 152.2 fps (+27%), 720p 1x +6.7%, picture identical. Only where the driver reports GPU upload heaps
    // (resizable BAR / Smart Access Memory on); elsewhere the ring stays in system memory. FM4_SYSTEM_MEMORY_RING=1: off.
    if (!getenv("FM4_SYSTEM_MEMORY_RING") && !getenv("REGPU_GPU_UPLOAD_RING"))
        _putenv_s("REGPU_GPU_UPLOAD_RING", "1");
    if (!getenv("FM4_NO_SHADER_VARIANTS"))
        for (const char* name : { "REGPU_SPLIT_ALPHA_TEST", "REGPU_FETCH_SPECIALIZE" })
            if (!getenv(name))
                _putenv_s(name, "1");
    // The Vulkan ReGPU's speed switches the FM3 port runs with (its gpu_host.cpp; each off by default in ReGPU). Set only
    // when not already in the environment; FM4_NO_FM3_DEFAULTS=1 leaves them all off (A/B).
    // - REGPU_DRAW_THREAD + _SERIAL: the command processor only parses and queues; a draw thread translates the draws.
    //   Without it FM4 ran everything on the command processor (one thread at ~97%, the limit in races).
    // - REGPU_ASYNC_PRESENT: the frame end (submit, present, fence) on the replay thread, not the draw thread.
    // - REGPU_VERTEX_ARENA: large vertex buffers kept in VRAM, copied again only when written (write watch).
    // - REGPU_LEAN_FETCH, REGPU_FETCH_VARIANTS: plain texture fetches take the short path / a pixel shader variant.
    // - REGPU_EXP_LAZY_LOOPS, REGPU_DIRECT_DYNAMIC_INDICES: per-draw work skipped where nothing changed.
    if (!getenv("FM4_NO_FM3_DEFAULTS"))
    {
        const char* fm3Defaults[] = { "REGPU_DRAW_THREAD", "REGPU_DRAW_THREAD_SERIAL", "REGPU_ASYNC_PRESENT", "REGPU_VERTEX_ARENA",
            "REGPU_LEAN_FETCH", "REGPU_FETCH_VARIANTS", "REGPU_EXP_LAZY_LOOPS", "REGPU_DIRECT_DYNAMIC_INDICES" };
        for (const char* name : fm3Defaults)
            if (!getenv(name))
                _putenv_s(name, "1");
    }
    // REGPU_FETCH_SPECIALIZE (live in ReGPU, off there by default): each pixel shader's texture fetches compiled for the
    // fetch constants of the draw (a variant per material: its words as literals), so a fetch is one sample plus only
    // the steps its constant asks for, instead of every format's path behind runtime tests (direct shaders: median ~3,200
    // GPU instructions a pixel shader). Fewer instructions to load at each of the ~1,170 pipeline switches a frame.
    // 1440p 4x grid, live in one boot: GPU 3.59 -> 3.10 ms; AI race at the same clock (3,050 MHz): 296.4 -> 307.4 fps,
    // 1% low 204 -> 213. Without it some boots ran the grid 1.3x slower on the GPU. Screenshot: 4 of 3.7 M pixels differ,
    // by at most 7/255. FM4_NO_FETCH_SPECIALIZE=1 leaves it off (or "switch REGPU_FETCH_SPECIALIZE 0" live).
    if (!getenv("FM4_NO_FETCH_SPECIALIZE") && !getenv("REGPU_FETCH_SPECIALIZE"))
        _putenv_s("REGPU_FETCH_SPECIALIZE", "1");
    // REGPU_VS_SPECIALIZE (live in ReGPU, off there by default): each vertex shader compiled for its draws' wrapper words
    // (viewport transform, index source, points, rectangles, each vertex fetch's endian swap and buffer) instead of testing
    // them for every vertex (median ~1,440 GPU instructions a vertex shader, ~710 specialised). 1440p 4x grid, live in
    // one boot, alternated: GPU 3.14 -> 3.01 ms; screenshot identical. FM4_NO_VS_SPECIALIZE=1 leaves it off.
    if (!getenv("FM4_NO_VS_SPECIALIZE") && !getenv("REGPU_VS_SPECIALIZE"))
        _putenv_s("REGPU_VS_SPECIALIZE", "1");
    host.resolveWriteback = [](const regpu::ResolveCopy& c) {
        regpu::Writeback w;
        static LiveSwitch off("FM4_NO_ROAD_WRITEBACK"); // live
        static const std::string part = getenv("FM4_ROAD_WRITEBACK") ? getenv("FM4_ROAD_WRITEBACK") : "";
        const bool image = c.format == 10 && c.width <= 512 && c.height <= 512 && part != "strips";
        const bool strip = c.width == 32 && c.height == 512 && part != "image";
        if (!off && !c.depth && (image || strip))
        {
            w.mode = regpu::Writeback::Mode::Deferred;
            // Each image written back, once (address, size, format, rows): which ones this rule catches.
            static std::mutex m;
            static std::set<uint64_t> seen;
            std::lock_guard lock(m);
            if (seen.insert(uint64_t(c.base) << 32 | c.width << 16 | c.height).second && seen.size() <= 400)
                Log("road writeback: %08X %ux%u format %u rows %d-%d", c.base, c.width, c.height, c.format, c.top, c.bottom);
            if (c.bottom > c.top)
            {
                w.y0 = uint32_t(std::max(c.top, 0));
                w.y1 = uint32_t(c.bottom);
            }
        }
        return w;
    };

    // The soft car shadow (fetch slot 11) is read through three tiles. The near tile (red) holds the
    // player's own shadow and the mid tile (green) the other cars' silhouettes, built from a depth copy
    // the shaders read as packed depth bytes (REGPU_DEPTH_AS_8888). The far tile (blue) reads the same
    // copy at about 2.3 times its scale (dark blocks on the road), so it reads lit.
    // FM4_NO_CASCADE_FIX=1 starts without this; each part is a live switch.
    if (!getenv("FM4_NO_CASCADE_FIX"))
    {
        if (!getenv("REGPU_CASCADE_SLOTS"))
            _putenv_s("REGPU_CASCADE_SLOTS", "11");
        for (const char* name : { "REGPU_DEPTH_AS_8888", "REGPU_CASCADE_B_LIT" })
            if (!getenv(name))
                _putenv_s(name, "1");
    }

    Log("gpu: ReGPU at %ux%u, msaa %u", config.width, config.height, config.msaa);
    regpu::Start();
}
