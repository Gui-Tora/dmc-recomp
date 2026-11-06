# Offline GS replay bench (introduced in PERF_003 E2; see README.md).
#
# Links against an existing, already-built PS2Recomp vendor tree (no runtime rebuild), like tools/vu1_replay:
#   cmake -S tools/gs_replay -B <build> -G "Visual Studio 17 2022" -A x64 -DPS2X_PREBUILT_DIR=<vendor build tree>
#   cmake --build <build> --config Release
# PS2X_PREBUILT_DIR is a vendor-root build of vendor/PS2Recomp at the patched_commit of upstream.lock.json, so the
# replayed GSCpuBackend is the same code as the game's.
cmake_minimum_required(VERSION 3.22) # ENVIRONMENT_MODIFICATION (selftest PATH)
project(gs_replay LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PS2X_PREBUILT_DIR "" CACHE PATH "Existing vendor-root build tree of vendor/PS2Recomp")
if(NOT PS2X_PREBUILT_DIR)
    message(FATAL_ERROR "set -DPS2X_PREBUILT_DIR=<vendor build tree>")
endif()

set(VENDOR "${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/PS2Recomp")
set(P "${PS2X_PREBUILT_DIR}")
set(FFMPEG_BIN "${P}/ThirdParty/ffmpeg-prefix/src/ffmpeg_external/bin")

add_executable(gs_replay main.cpp function_table_stub.cpp)
target_include_directories(gs_replay PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${VENDOR}/ps2xRuntime/include"
    "${VENDOR}/ps2xRuntime/src/lib"
    "${VENDOR}/ps2xIOP/include"
)
target_compile_options(gs_replay PRIVATE /utf-8)
target_link_options(gs_replay PRIVATE /MAP) # symbolization of --profile samples
target_link_libraries(gs_replay PRIVATE
    "${P}/ps2xRuntime/$<CONFIG>/ps2_runtime.lib"
    "${P}/ps2xIOP/$<CONFIG>/ps2_iop.lib"
    "${P}/_deps/raylib-build/raylib/$<CONFIG>/raylib.lib"
    "${FFMPEG_BIN}/avcodec.lib" "${FFMPEG_BIN}/avformat.lib" "${FFMPEG_BIN}/avutil.lib"
    "${FFMPEG_BIN}/swresample.lib" "${FFMPEG_BIN}/swscale.lib"
    opengl32 glu32 winmm bcrypt secur32 ws2_32 user32 gdi32 advapi32 ole32 oleaut32 shell32 uuid comdlg32
)

# Automated check: synthetic scenes recorded through the real GS frontend and replayed (ctest -C Release).
# The FFmpeg DLLs of the prebuilt tree must be on PATH at run time.
enable_testing()
set(GS_REPLAY_SELFTEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/selftest")
file(MAKE_DIRECTORY "${GS_REPLAY_SELFTEST_DIR}")
add_test(NAME gs_replay_selftest COMMAND gs_replay selftest "${GS_REPLAY_SELFTEST_DIR}")
set_tests_properties(gs_replay_selftest PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${FFMPEG_BIN}")
