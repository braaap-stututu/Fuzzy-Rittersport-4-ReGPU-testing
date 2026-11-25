cmake_minimum_required(VERSION 3.25)
project(fm4 C CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "fm4 needs clang-cl (the recompiled code uses clang builtins)")
endif()
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)

add_subdirectory(thirdparty/XenonRecomp EXCLUDE_FROM_ALL)

# Recompiled game code, produced by XenonRecomp from your own XEX files (tools/recomp-fm4.sh;
# never committed): one static library per image so each keeps its own ppc_config.h. The
# first image is default.xex, the others are the modules it loads at run time.
# FM4_OUT_DIR: the folder (in the source tree) holding the recompiled images, default out.
set(FM4_OUT_DIR "out" CACHE STRING "folder with the recompiled images (out/ppc, out/ppc_xmf, out/ppc_sph)")
set(FM4_IMAGES "fm4:ppc" "fm4_xmediafacade:ppc_xmf" "fm4_speechfacade:ppc_sph")
set(FM4_CONFIGS "")
set(FM4_SHARED_HEADERS "")
set(FM4_PPC_LIBS "")
set(FM4_PPC_DIRS "")
foreach(image ${FM4_IMAGES})
    string(REPLACE ":" ";" parts "${image}")
    list(GET parts 0 name)
    list(GET parts 1 dir)
    set(ppc_dir "${CMAKE_SOURCE_DIR}/${FM4_OUT_DIR}/${dir}")
    file(GLOB sources CONFIGURE_DEPENDS "${ppc_dir}/*.cpp")
    if(NOT sources)
        message(FATAL_ERROR "${FM4_OUT_DIR}/${dir} is empty: run tools/recomp-fm4.sh first (see docs/FM4-RECOMPILE.md)")
    endif()
    add_library(${name}_ppc STATIC ${sources})
    target_include_directories(${name}_ppc PRIVATE "${ppc_dir}" thirdparty/XenonRecomp/thirdparty/simde)
    # -mcx16: MSVC's <atomic> marks its functions target("cx16"); without it clang-cl calls every std::atomic access and
    # fence (the recompiled code's sync/lwsync) instead of inlining it (FM3 port: +10% CPU-bound).
    target_compile_options(${name}_ppc PRIVATE -Wno-everything -msse4.1 -mcx16 -fno-strict-aliasing
        -gline-tables-only "/FI${CMAKE_SOURCE_DIR}/src/ppc_hooks.h")
    # The recompiled code reads the host's raw timestamp counter for mftb; give it the console's
    # timebase instead (tools/patch_mftb.py, Fm2Timebase in src/kernel/runtime.cpp).
    execute_process(COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_SOURCE_DIR}/tools/patch_mftb.py" "${ppc_dir}"
                    OUTPUT_VARIABLE mftb_result RESULT_VARIABLE mftb_code OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT mftb_code EQUAL 0)
        message(FATAL_ERROR "tools/patch_mftb.py failed on ${ppc_dir}")
    endif()
    message(STATUS "${name} mftb: ${mftb_result}")
    list(APPEND FM4_CONFIGS "${CMAKE_SOURCE_DIR}/config/${name}.toml")
    list(APPEND FM4_SHARED_HEADERS "${ppc_dir}/ppc_recomp_shared.h")
    list(APPEND FM4_PPC_LIBS ${name}_ppc)
endforeach()

# Import stubs for all three images, and the table of recompiled images (src/recomp_modules.h).
set(FM4_GENERATED "${CMAKE_BINARY_DIR}/generated")
add_custom_command(OUTPUT "${FM4_GENERATED}/stubs.cpp"
    COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_SOURCE_DIR}/tools/genstubs.py" ${FM4_SHARED_HEADERS} "${FM4_GENERATED}/stubs.cpp"
    DEPENDS ${FM4_SHARED_HEADERS} "${CMAKE_SOURCE_DIR}/tools/genstubs.py"
    COMMENT "Generating import stubs")
add_custom_command(OUTPUT "${FM4_GENERATED}/recomp_modules.cpp"
    COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_SOURCE_DIR}/tools/genmodules.py" "${FM4_GENERATED}/recomp_modules.cpp" ${FM4_CONFIGS}
    DEPENDS ${FM4_CONFIGS} ${FM4_SHARED_HEADERS} "${CMAKE_SOURCE_DIR}/tools/genmodules.py"
    COMMENT "Generating the recompiled image table")

# Runtime sources. src/hooks/fm2 holds FM2's address-bound hooks (they override FM2 sub_
# functions, which would hijack unrelated FM4 code); they stay out of the build unless asked.
option(FM4_BUILD_FM2_HOOKS "Compile src/hooks/fm2 (reference only; breaks an FM4 build)" OFF)
file(GLOB_RECURSE FM4_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(FILTER FM4_SOURCES EXCLUDE REGEX "src/kernel/stubs\\.cpp$")
if(NOT FM4_BUILD_FM2_HOOKS)
    list(FILTER FM4_SOURCES EXCLUDE REGEX "src/hooks/fm2/")
endif()
list(APPEND FM4_SOURCES "${FM4_GENERATED}/stubs.cpp" "${FM4_GENERATED}/recomp_modules.cpp")

# The GPU: ReGPU (thirdparty/ReGPU, MIT), a static library that provides the Vd* imports. It
# builds against XenonRecomp and the main image's ppc_config.h, and needs the AMD FidelityFX SDK
# (MIT) for FSR 1; FSR 3 and NVIDIA DLSS are used when their SDKs are in thirdparty/ (see README).
set(FM4_FFX "${CMAKE_SOURCE_DIR}/thirdparty/fidelityfx/Kits/FidelityFX")
set(FM4_DLSS "${CMAKE_SOURCE_DIR}/thirdparty/dlss")
set(REGPU_XENONRECOMP "${CMAKE_SOURCE_DIR}/thirdparty/XenonRecomp")
set(REGPU_PPC_DIR "${CMAKE_SOURCE_DIR}/${FM4_OUT_DIR}/ppc")
set(REGPU_FFX "${FM4_FFX}")
# The Vulkan ReGPU (braaap-stututu/ReGPU, branch vulkan; FM4_REGPU_DIR) needs the SDK's Vulkan DLLs (2.4.0 or newer).
if(EXISTS "${FM4_FFX}/signedbin/amd_fidelityfx_upscaler_vk.dll")
    set(REGPU_FSR3 ON)
endif()
# Vulkan: the translated shaders are compiled to SPIR-V by DXC at run time (dxcompiler.dll + dxil.dll beside fm4.exe; a
# DirectXShaderCompiler release or the Vulkan SDK's, FM4_DXC_DIR); ReGPU's own compute shaders need dxc.exe.
set(FM4_DXC_DIR "$ENV{VULKAN_SDK}/Bin" CACHE PATH "Folder with dxc.exe, dxcompiler.dll and dxil.dll (SPIR-V output)")
if(EXISTS "${FM4_DXC_DIR}/dxc.exe")
    set(REGPU_DXC_EXE "${FM4_DXC_DIR}/dxc.exe" CACHE FILEPATH "DXC with SPIR-V output (for ReGPU's embedded compute shaders)")
endif()
if(EXISTS "${FM4_DLSS}/include/nvsdk_ngx.h")
    set(REGPU_DLSS ON)
    set(REGPU_DLSS_DIR "${FM4_DLSS}")
endif()
# Windows SDKs before 10.0.26100 lack ID3D12Device13: ReGPU then takes d3d12.h from Microsoft's
# DirectX-Headers (MIT) in thirdparty/directx-headers, if it is there (see README).
if(EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/directx-headers/include/directx/d3d12.h")
    set(REGPU_DIRECTX_HEADERS "${CMAKE_SOURCE_DIR}/thirdparty/directx-headers")
endif()
# FM4_REGPU_DIR: which ReGPU to build (default the vendored D3D12 copy in thirdparty/ReGPU).
set(FM4_REGPU_DIR "${CMAKE_SOURCE_DIR}/thirdparty/ReGPU" CACHE PATH "ReGPU source folder")
add_subdirectory("${FM4_REGPU_DIR}" regpu)

add_executable(fm4 ${FM4_SOURCES} "${CMAKE_SOURCE_DIR}/src/fm4.rc")

# Profile-guided optimisation of ReGPU and FM4's own code (from the FM3 port, where ReGPU's gained ~6% on a Steam Deck):
# FM4_PGO=gen instruments both (start with LLVM_PROFILE_FILE=<dir>/fm4-%p.profraw, play, then debug server
# "env FM4_PGO_WRITE 1" writes the profile: src/pgo.cpp), then llvm-profdata merge -output=pgo/fm4.profdata <dir>/*.profraw;
# FM4_PGO=use builds with FM4_PGO_PROFILE. The recompiled game code (fm4_ppc) is not changed, so switching rebuilds only
# ReGPU and src/.
set(FM4_PGO "off" CACHE STRING "ReGPU + FM4 profile-guided optimisation: off, gen or use")
set_property(CACHE FM4_PGO PROPERTY STRINGS off gen use)
set(FM4_PGO_PROFILE "${CMAKE_SOURCE_DIR}/pgo/fm4.profdata" CACHE FILEPATH "PGO profile (llvm-profdata merge output)")
if(FM4_PGO STREQUAL "gen")
    target_compile_options(regpu PRIVATE -fprofile-instr-generate)
    target_compile_options(fm4 PRIVATE -fprofile-instr-generate)
    get_filename_component(FM4_LLVM_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    file(GLOB FM4_PROFILE_RT "${FM4_LLVM_BIN}/../lib/clang/*/lib/windows/clang_rt.profile-x86_64.lib")
    if(NOT FM4_PROFILE_RT)
        message(FATAL_ERROR "FM4_PGO=gen: clang_rt.profile-x86_64.lib not found next to ${CMAKE_CXX_COMPILER}")
    endif()
    list(GET FM4_PROFILE_RT 0 FM4_PROFILE_RT)
    target_link_libraries(fm4 PRIVATE "${FM4_PROFILE_RT}")
    target_compile_definitions(fm4 PRIVATE FM4_PGO_GEN=1)
elseif(FM4_PGO STREQUAL "use")
    if(NOT EXISTS "${FM4_PGO_PROFILE}")
        message(FATAL_ERROR "FM4_PGO=use: no profile at ${FM4_PGO_PROFILE}")
    endif()
    foreach(t regpu fm4)
        target_compile_options(${t} PRIVATE "-fprofile-instr-use=${FM4_PGO_PROFILE}" -Wno-profile-instr-out-of-date
            -Wno-profile-instr-unprofiled -Wno-backend-plugin)
    endforeach()
endif()
target_include_directories(fm4 PRIVATE src "${FM4_GENERATED}"
    "${CMAKE_SOURCE_DIR}/${FM4_OUT_DIR}/ppc" thirdparty/XenonRecomp/thirdparty/simde)
target_link_libraries(fm4 PRIVATE regpu)
if(REGPU_FSR3)
    add_custom_command(TARGET fm4 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FM4_FFX}/signedbin/amd_fidelityfx_loader_vk.dll" "${FM4_FFX}/signedbin/amd_fidelityfx_upscaler_vk.dll" $<TARGET_FILE_DIR:fm4>)
endif()
foreach(FM4_DXC_DLL dxcompiler.dll dxil.dll)
    if(EXISTS "${FM4_DXC_DIR}/${FM4_DXC_DLL}")
        add_custom_command(TARGET fm4 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FM4_DXC_DIR}/${FM4_DXC_DLL}" $<TARGET_FILE_DIR:fm4>)
    endif()
endforeach()
if(REGPU_DLSS)
    add_custom_command(TARGET fm4 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FM4_DLSS}/lib/Windows_x86_64/rel/nvngx_dlss.dll" $<TARGET_FILE_DIR:fm4>)
endif()
# ReGPU compiles the translated shaders with the redistributable d3dcompiler_47.dll beside the exe.
if(EXISTS "${REGPU_D3DCOMPILER}")
    add_custom_command(TARGET fm4 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${REGPU_D3DCOMPILER}" $<TARGET_FILE_DIR:fm4>)
endif()
# FM4_FRAME_POINTERS=ON: ReGPU + FM4 keep frame pointers, so `perf record --call-graph fp` shows who calls into
# ntdll (Steam Deck profiling). The recompiled game code is not changed. Off by default.
option(FM4_FRAME_POINTERS "ReGPU + FM4 with frame pointers (call-graph profiling)" OFF)
if(FM4_FRAME_POINTERS)
    foreach(t regpu fm4)
        target_compile_options(${t} PRIVATE "SHELL:-Xclang -mframe-pointer=all")
    endforeach()
endif()
target_link_libraries(fm4 PRIVATE ${FM4_PPC_LIBS} XenonUtils synchronization winmm onecore)
target_compile_options(fm4 PRIVATE -mcx16)
target_compile_definitions(fm4 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(fm4 PRIVATE -gline-tables-only)
target_link_options(fm4 PRIVATE /STACK:0x1000000 /DEBUG)
target_link_libraries(fm4 PRIVATE dbghelp xaudio2 bcrypt winhttp)
# Racing wheels, pedals, shifters and handbrakes (src/input/wheel, DirectInput).
target_link_libraries(fm4 PRIVATE dinput8 dxguid)

# XMA decoding through FFmpeg's xma2 decoder (LGPL, linked dynamically): a shared build
# with include/, lib/ and bin/ in thirdparty/ffmpeg (see README).
set(FM4_FFMPEG "${CMAKE_SOURCE_DIR}/thirdparty/ffmpeg")
if(NOT EXISTS "${FM4_FFMPEG}/include/libavcodec/avcodec.h")
    message(FATAL_ERROR "thirdparty/ffmpeg is missing: extract an FFmpeg win64 LGPL shared build there (see README)")
endif()
target_include_directories(fm4 PRIVATE "${FM4_FFMPEG}/include")
target_link_libraries(fm4 PRIVATE "${FM4_FFMPEG}/lib/avcodec.lib" "${FM4_FFMPEG}/lib/avutil.lib")
file(GLOB FM4_FFMPEG_DLLS "${FM4_FFMPEG}/bin/avcodec-*.dll" "${FM4_FFMPEG}/bin/avutil-*.dll" "${FM4_FFMPEG}/bin/swresample-*.dll")
add_custom_command(TARGET fm4 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FM4_FFMPEG_DLLS} $<TARGET_FILE_DIR:fm4>)
