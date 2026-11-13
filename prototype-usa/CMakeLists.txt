# majin - ReXGlue Recompiled Project (American / NTSC-U 0.0.0.2)
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(majin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Target microarchitecture optimizations (Issue #19)
# Requires x86-64-v3 (AVX2, FMA, BMI1/2, MOVBE). CPUs lacking AVX2 are unsupported.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64" OR CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        add_compile_options(
            -march=x86-64-v3
            -mavx2
            -mfma
            -mbmi
            -mbmi2
            -ffp-contract=fast
            -fno-trapping-math
        )
    elseif(MSVC)
        add_compile_options(/arch:AVX2 /fp:fast)
    endif()
endif()

# ThinLTO (Link-Time Optimization) for Release builds (Issue #19)
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
    if(ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
        message(STATUS "ThinLTO (IPO) enabled for ${CMAKE_BUILD_TYPE} build.")
    else()
        message(STATUS "ThinLTO (IPO) probe returned unsupported: ${ipo_error}")
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            add_compile_options(-flto=thin)
            add_link_options(-flto=thin -fuse-ld=lld)
            message(STATUS "Fallback ThinLTO (-flto=thin -fuse-ld=lld) enabled for Clang.")
        endif()
    endif()
endif()

option(MAJIN_ENABLE_DIAGNOSTICS "Enable high-frequency diagnostic tracing hooks in core runtime" OFF)
if(MAJIN_ENABLE_DIAGNOSTICS)
    add_compile_definitions(MAJIN_ENABLE_DIAGNOSTICS=1)
    message(STATUS "High-frequency diagnostic tracing ENABLED.")
endif()

add_compile_definitions(MAJIN_EDITION_USA=1)

include(generated/rexglue.cmake)

set(MAJIN_SHARED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")

# Sources from unified src/
set(MAJIN_SOURCES
    ${MAJIN_SHARED_DIR}/main.cpp
    ${MAJIN_SHARED_DIR}/diagnostics.cpp
    ${MAJIN_SHARED_DIR}/timing.cpp
    ${MAJIN_SHARED_DIR}/cutscene.cpp
    ${MAJIN_SHARED_DIR}/vulkan_hooks.cpp
    ${MAJIN_SHARED_DIR}/scene_resolution.cpp
    ${MAJIN_SHARED_DIR}/step_fix.cpp
    ${MAJIN_SHARED_DIR}/anim_speed.cpp
    ${MAJIN_SHARED_DIR}/frame_timer.cpp
    ${MAJIN_SHARED_DIR}/camera_fov.cpp
    ${MAJIN_SHARED_DIR}/post_effects.cpp
    ${MAJIN_SHARED_DIR}/in_game_menu.cpp
)

if(WIN32)
    add_executable(majin WIN32 ${MAJIN_SOURCES})
    target_link_libraries(majin PRIVATE winmm)
else()
    add_executable(majin ${MAJIN_SOURCES})
endif()

target_include_directories(majin PRIVATE "${MAJIN_SHARED_DIR}")
rexglue_setup_target(majin GPU_PLUGINS xenos)
if(TARGET majin_recomp)
    target_include_directories(majin_recomp PRIVATE "${MAJIN_SHARED_DIR}")
endif()
target_include_directories(majin PRIVATE "${CMAKE_PREFIX_PATH}/include/renderdoc")

# Separate opt-in research build. Never enabled by the normal build script.
option(MAJIN_1080P_STARTUP_PROBE "Probe engine startup dimensions; not validated 1080p" OFF)
if(MAJIN_1080P_STARTUP_PROBE)
    target_sources(majin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../experiments/1080p/startup_probe.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/../experiments/1080p/game_trace.cpp")
endif()
