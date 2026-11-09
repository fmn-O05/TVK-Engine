cmake_minimum_required(VERSION 3.20)
project(TVK_Engine VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================================
# Build Options
# ============================================================================
option(TVK_BUILD_EXAMPLES "Build TVK Engine example applications" ON)
option(TVK_BUILD_TESTS    "Build TVK Engine unit and integration tests" OFF)
option(TVK_ENABLE_SIMD    "Enable AVX2/FMA SIMD acceleration on x86_64" ON)

# ============================================================================
# Engine Core Library Sources (Organized by Subsystem Layer)
# ============================================================================
set(TVK_CORE_SOURCES
    # Layer 0: Core (Threading, Job System, Memory, Profiling)
    src/core/JobSystem.cpp

    # Layer 1: Platform Abstraction Layer (Terminal TTY, Input, Timing)
    src/platform/PAL.cpp

    # Layer 2: Math & SIMD Kernels
    src/math/Math.cpp

    # Layer 3: 3D Software Renderer & Terminal Presentation
    src/renderer/Camera.cpp
    src/renderer/CommandBuffer.cpp
    src/renderer/Primitives.cpp
    src/renderer/Rasterizer.cpp
    src/renderer/Renderer.cpp
    src/renderer/TerminalPresenter.cpp

    # Layer 4: Scene & ECS (Phase 4)
    # Layer 5: Physics & Collision (Phase 5)
    # Layer 6: 2D Canvas & Terminal UI (Phase 3 / Phase 6)
    # Layer 7: Asset Pipeline (Phase 6)
    # Layer 8: Audio Subsystem (Phase 7)
    # Layer 9: Engine Runtime & Application Host (Phase 1 / Phase 4)
)

add_library(tvk_core STATIC ${TVK_CORE_SOURCES})
add_library(tvk::core ALIAS tvk_core)

target_include_directories(tvk_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# ============================================================================
# Compiler & Architecture Flags
# ============================================================================
function(tvk_configure_target target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE
            /O2 /W4
            /wd4324 # structure was padded due to alignment specifier
            /wd4100 # unreferenced formal parameter
        )
        if(TVK_ENABLE_SIMD)
            target_compile_options(${target_name} PRIVATE /arch:AVX2)
        endif()
    else()
        target_compile_options(${target_name} PRIVATE
            -O3 -Wall -Wextra
            -fstrict-aliasing
            -fno-exceptions
        )
        if(TVK_ENABLE_SIMD AND CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|i686)")
            target_compile_options(${target_name} PRIVATE -march=native -mavx2 -mfma)
        endif()

        if(MINGW)
            target_link_options(${target_name} PUBLIC
                -static-libgcc
                -static-libstdc++
                -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic
            )
        endif()

        if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
            target_link_options(${target_name} PRIVATE -Wl,--large-address-aware)
        endif()
    endif()
endfunction()

tvk_configure_target(tvk_core)

if(WIN32)
    target_link_libraries(tvk_core PRIVATE winmm)
endif()

find_package(Threads REQUIRED)
target_link_libraries(tvk_core PUBLIC Threads::Threads)

# ============================================================================
# Example Applications
# ============================================================================
if(TVK_BUILD_EXAMPLES)
    add_executable(sandbox examples/Sandbox.cpp)
    target_link_libraries(sandbox PRIVATE tvk::core)
    tvk_configure_target(sandbox)

    add_executable(asteroids examples/Asteroids_Demo.cpp)
    target_link_libraries(asteroids PRIVATE tvk::core)
    tvk_configure_target(asteroids)
endif()

# ============================================================================
# Tests (Phase 1+)
# ============================================================================
if(TVK_BUILD_TESTS)
    enable_testing()
endif()

# ============================================================================
# Configuration Summary
# ============================================================================
message(STATUS "")
message(STATUS "╔══════════════════════════════════════════════════════╗")
message(STATUS "║             TVK Engine Build Configuration           ║")
message(STATUS "╠══════════════════════════════════════════════════════╣")
message(STATUS "║  Version       : ${PROJECT_VERSION}")
message(STATUS "║  Platform      : ${CMAKE_SYSTEM_NAME} (${CMAKE_SYSTEM_PROCESSOR})")
message(STATUS "║  Compiler      : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "║  Build Type    : ${CMAKE_BUILD_TYPE}")
message(STATUS "║  SIMD (AVX2)   : ${TVK_ENABLE_SIMD}")
message(STATUS "║  Examples      : ${TVK_BUILD_EXAMPLES}")
message(STATUS "║  Tests         : ${TVK_BUILD_TESTS}")
message(STATUS "╚══════════════════════════════════════════════════════╝")
message(STATUS "")
