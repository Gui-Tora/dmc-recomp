# GAMEPLAY_001K offline VU1 replay bench.
#
# Links against an existing, already-built PS2Recomp vendor tree (no runtime rebuild):
#   cmake -S tools/vu1_replay -B <build> -G "Visual Studio 17 2022" -A x64 -DPS2X_PREBUILT_DIR=<vendor build tree>
#   cmake --build <build> --config Debug
# PS2X_PREBUILT_DIR is a vendor-root build of vendor/PS2Recomp (PS2X_BUILD_RUNTIME=ON) at the
# patched_commit recorded in upstream.lock.json; only the matching configuration can be linked.
cmake_minimum_required(VERSION 3.21)
project(vu1_replay LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PS2X_PREBUILT_DIR "" CACHE PATH "Existing vendor-root build tree of vendor/PS2Recomp")
if(NOT PS2X_PREBUILT_DIR)
    message(FATAL_ERROR "set -DPS2X_PREBUILT_DIR=<vendor build tree>")
endif()

set(VENDOR "${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/PS2Recomp")
set(P "${PS2X_PREBUILT_DIR}")
set(FFMPEG_BIN "${P}/ThirdParty/ffmpeg-prefix/src/ffmpeg_external/bin")

# Optional C++ produced by gen_vu1c.py from game microcode (kept outside version control, e.g. under
# analysis/local). Without it the bench only replays the interpreter.
set(VU1C_GENERATED "" CACHE STRING "Generated compiled-microprogram sources (;-separated, outside the repo)")

add_executable(vu1_replay main.cpp function_table_stub.cpp ${VU1C_GENERATED})
target_include_directories(vu1_replay PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${VENDOR}/ps2xRuntime/include"
    "${VENDOR}/ps2xRuntime/src/lib"
    "${VENDOR}/ps2xIOP/include"
)
target_compile_options(vu1_replay PRIVATE /utf-8)
target_link_libraries(vu1_replay PRIVATE
    "${P}/ps2xRuntime/$<CONFIG>/ps2_runtime.lib"
    "${P}/ps2xIOP/$<CONFIG>/ps2_iop.lib"
    "${P}/_deps/raylib-build/raylib/$<CONFIG>/raylib.lib"
    "${FFMPEG_BIN}/avcodec.lib" "${FFMPEG_BIN}/avformat.lib" "${FFMPEG_BIN}/avutil.lib"
    "${FFMPEG_BIN}/swresample.lib" "${FFMPEG_BIN}/swscale.lib"
    opengl32 glu32 winmm bcrypt secur32 ws2_32 user32 gdi32 advapi32 ole32 oleaut32 shell32 uuid comdlg32
)
