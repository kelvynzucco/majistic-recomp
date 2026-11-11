cmake_minimum_required(VERSION 3.25)
project(gpl_spike LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../sdk/win-amd64")
set(SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../source/rexglue-sdk")

include_directories(
    "${SRC_ROOT}/thirdparty/vulkan-headers/include"
    "${SDK_ROOT}/include"
    "${SRC_ROOT}/src"
    "${SRC_ROOT}/thirdparty/glslang"
    "${SRC_ROOT}/thirdparty/renderdoc"
    "${SRC_ROOT}/thirdparty/fmt/include"
    "${SRC_ROOT}/thirdparty/spdlog/include"
    "${SRC_ROOT}/thirdparty/xxHash"
)

set(TRANSLATOR_SOURCES
    "${SRC_ROOT}/src/graphics/pipeline/shader/shader.cpp"
    "${SRC_ROOT}/src/graphics/pipeline/shader/spirv.cpp"
    "${SRC_ROOT}/src/graphics/pipeline/shader/spirv_builder.cpp"
    "${SRC_ROOT}/src/graphics/pipeline/shader/spirv_translator.cpp"
    "${SRC_ROOT}/src/graphics/pipeline/shader/spirv_translator_alu.cpp"
    "${SRC_ROOT}/src/graphics/pipeline/shader/spirv_translator_fetch.cpp"
    "${SRC_ROOT}/src/graphics/pipeline/shader/spirv_translator_memexport.cpp"
    "${SRC_ROOT}/src/graphics/pipeline/shader/spirv_translator_rb.cpp"
    "${SRC_ROOT}/src/graphics/pipeline/shader/translator.cpp"
    "${SRC_ROOT}/src/graphics/pipeline/shader/translator_disasm.cpp"
    "${SRC_ROOT}/src/graphics/format/ucode.cpp"
    "${SRC_ROOT}/src/graphics/registers.cpp"
    "${SRC_ROOT}/src/graphics/flags.cpp"
    "${SRC_ROOT}/src/graphics/util/draw.cpp"
)

add_executable(gpl_spike
    main.cpp
    ${TRANSLATOR_SOURCES}
)

target_compile_definitions(gpl_spike PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    VK_USE_PLATFORM_WIN32_KHR
    VK_ENABLE_BETA_EXTENSIONS
    VULKAN_HPP_NO_TO_STRING
    REX_INLINE_BIT_SCANS=1
)

if(MSVC)
    target_compile_options(gpl_spike PRIVATE /EHsc)
endif()

target_link_directories(gpl_spike PRIVATE "${SDK_ROOT}/lib")
target_link_libraries(gpl_spike PRIVATE
    rexruntime
    fmt
    spdlog
    xxhash
    SPIRV
    glslang
    MachineIndependent
    GenericCodeGen
    OSDependent
    OGLCompiler
)
