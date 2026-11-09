# majin - ReXGlue Recompiled Project (European / PAL 0.0.0.3)
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(majin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(MAJIN_EDITION_PAL=1)

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
