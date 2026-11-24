// Profile-guided optimisation of ReGPU and FM4's own code (from the FM3 port). Only in a build configured with -DFM4_PGO=gen
// (ReGPU compiled with -fprofile-instr-generate): the game may end through ExitProcess, which skips the profile
// runtime's exit handler, so the profile is written on request: debug server "env FM4_PGO_WRITE 1" (polled twice a second)
// writes it to LLVM_PROFILE_FILE (default: default.profraw in the working directory). How to record and use a
// profile: CMakeLists.txt (FM4_PGO).
#ifdef FM4_PGO_GEN
#include <windows.h>

#include <thread>

#include "log.h"

extern "C" int __llvm_profile_write_file(void);

namespace
{
struct PgoWriter
{
    PgoWriter()
    {
        std::thread([] {
            bool last = false;
            for (;;)
            {
                Sleep(500);
                char v[4] = {};
                const bool on = GetEnvironmentVariableA("FM4_PGO_WRITE", v, sizeof(v)) && v[0] == '1';
                if (on && !last)
                {
                    const int r = __llvm_profile_write_file();
                    Log("pgo: profile written (%d)", r);
                }
                last = on;
            }
        }).detach();
    }
} g_pgoWriter;
} // namespace
#endif
