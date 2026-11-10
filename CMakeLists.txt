cmake_minimum_required(VERSION 3.20)
project(TVK_Engine VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================================
# Build Options
# ============================================================================
option(TVK_BUILD_EXAMPLES "Build TVK Engine example applications" ON)
option(TVK_BUILD_TESTS    "Build TVK Engine unit and integration tests" ON)
option(TVK_ENABLE_SIMD    "Enable AVX2/FMA SIMD acceleration on x86_64" ON)
option(TVK_MARCH_NATIVE   "Enable host-specific -march=native optimization" OFF)

# ============================================================================
# Engine Core Library Sources (Organized by Subsystem Layer)
# ============================================================================
set(TVK_CORE_SOURCES
    # Layer 0: Core (Threading, Job System, Memory, Profiling)
    src/core/JobSystem.cpp
    src/core/Log.cpp

    # Layer 1: Platform Abstraction Layer (Terminal TTY, Input, Timing)
    src/platform/PAL.cpp
    src/platform/Input.cpp

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
    src/app/Engine.cpp
)

add_library(tvk_core STATIC ${TVK_CORE_SOURCES})
add_library(tvk::core ALIAS tvk_core)
add_library(tvk_engine ALIAS tvk_core)

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
            $<$<CONFIG:Debug>:/Od /Zi>
            $<$<NOT:$<CONFIG:Debug>>:/O2>
            /W4
            /EHs-c-
            /wd4324 # structure was padded due to alignment specifier
            /wd4100 # unreferenced formal parameter
        )
        target_compile_definitions(${target_name} PUBLIC _HAS_EXCEPTIONS=0)
        if(TVK_ENABLE_SIMD AND CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64)")
            target_compile_options(${target_name} PUBLIC /arch:AVX2)
            target_compile_definitions(${target_name} PUBLIC TVK_SIMD_AVX2=1)
        endif()
    else()
        target_compile_options(${target_name} PRIVATE
            $<$<CONFIG:Debug>:-O0 -g>
            $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<STREQUAL:$<CONFIG>,>>:-O3>
            -Wall -Wextra -Wpedantic
            -fstrict-aliasing
            -fno-exceptions
        )
        if(TVK_ENABLE_SIMD AND CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|i686)")
            target_compile_options(${target_name} PUBLIC -mavx2 -mfma)
            target_compile_definitions(${target_name} PUBLIC TVK_SIMD_AVX2=1)
        endif()
        if(TVK_MARCH_NATIVE OR TVK_ENABLE_MARCH_NATIVE)
            target_compile_options(${target_name} PRIVATE -march=native)
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
    add_custom_command(TARGET sandbox POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:sandbox>
            $<TARGET_FILE_DIR:sandbox>/Sandbox${CMAKE_EXECUTABLE_SUFFIX}
    )

    add_executable(asteroids examples/Asteroids_Demo.cpp)
    target_link_libraries(asteroids PRIVATE tvk::core)
    tvk_configure_target(asteroids)
    add_custom_command(TARGET asteroids POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:asteroids>
            $<TARGET_FILE_DIR:asteroids>/Asteroids_Demo${CMAKE_EXECUTABLE_SUFFIX}
    )
endif()

# ============================================================================
# Tests (Automated Unit & Headless Verification Suite)
# ============================================================================
if(TVK_BUILD_TESTS)
    enable_testing()

    set(TVK_TEST_LAUNCHER "")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_FLAGS MATCHES "fsanitize=thread")
        find_program(TVK_SETARCH_BIN setarch)
        if(TVK_SETARCH_BIN)
            set(TVK_TEST_LAUNCHER ${TVK_SETARCH_BIN} ${CMAKE_SYSTEM_PROCESSOR} -R)
        endif()
    endif()

    add_executable(test_core tests/test_core.cpp)
    target_link_libraries(test_core PRIVATE tvk::core)
    tvk_configure_target(test_core)
    add_test(NAME test_core COMMAND ${TVK_TEST_LAUNCHER} $<TARGET_FILE:test_core>)

    add_executable(test_math tests/test_math.cpp)
    target_link_libraries(test_math PRIVATE tvk::core)
    tvk_configure_target(test_math)
    add_test(NAME test_math COMMAND ${TVK_TEST_LAUNCHER} $<TARGET_FILE:test_math>)

    add_executable(test_renderer tests/test_renderer.cpp)
    target_link_libraries(test_renderer PRIVATE tvk::core)
    tvk_configure_target(test_renderer)
    add_test(NAME test_renderer COMMAND ${TVK_TEST_LAUNCHER} $<TARGET_FILE:test_renderer>)

    add_executable(test_input tests/test_input.cpp)
    target_link_libraries(test_input PRIVATE tvk::core)
    tvk_configure_target(test_input)
    add_test(NAME test_input COMMAND ${TVK_TEST_LAUNCHER} $<TARGET_FILE:test_input>)

    add_executable(test_adversarial_input tests/test_adversarial_input.cpp)
    target_link_libraries(test_adversarial_input PRIVATE tvk::core)
    tvk_configure_target(test_adversarial_input)
    add_test(NAME test_adversarial_input COMMAND ${TVK_TEST_LAUNCHER} $<TARGET_FILE:test_adversarial_input>)

    add_executable(test_adversarial_m1_2 tests/test_adversarial_m1_2.cpp)
    target_link_libraries(test_adversarial_m1_2 PRIVATE tvk::core)
    tvk_configure_target(test_adversarial_m1_2)
    add_test(NAME test_adversarial_m1_2 COMMAND ${TVK_TEST_LAUNCHER} $<TARGET_FILE:test_adversarial_m1_2>)

    add_executable(test_challenger_m1_it2 tests/test_challenger_m1_it2.cpp)
    target_link_libraries(test_challenger_m1_it2 PRIVATE tvk::core)
    tvk_configure_target(test_challenger_m1_it2)
    add_test(NAME test_challenger_m1_it2 COMMAND ${TVK_TEST_LAUNCHER} $<TARGET_FILE:test_challenger_m1_it2>)

    add_executable(test_adversarial_challenger_it2 tests/test_adversarial_challenger_it2.cpp)
    target_link_libraries(test_adversarial_challenger_it2 PRIVATE tvk::core)
    tvk_configure_target(test_adversarial_challenger_it2)
    add_test(NAME test_adversarial_challenger_it2 COMMAND ${TVK_TEST_LAUNCHER} $<TARGET_FILE:test_adversarial_challenger_it2>)

    add_executable(test_rasterizer_extended tests/test_rasterizer_extended.cpp)
    target_link_libraries(test_rasterizer_extended PRIVATE tvk::core)
    tvk_configure_target(test_rasterizer_extended)
    add_test(NAME test_rasterizer_extended COMMAND ${TVK_TEST_LAUNCHER} $<TARGET_FILE:test_rasterizer_extended>)

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
message(STATUS "║  March Native  : ${TVK_MARCH_NATIVE}")
message(STATUS "║  Examples      : ${TVK_BUILD_EXAMPLES}")
message(STATUS "║  Tests         : ${TVK_BUILD_TESTS}")
message(STATUS "╚══════════════════════════════════════════════════════╝")
message(STATUS "")
