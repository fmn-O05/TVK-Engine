cmake_minimum_required(VERSION 3.20)
project(TVK_Engine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(TVK_USE_NOTCURSES  "Build notcurses backend (MinGW/GCC/Clang only)" OFF)
option(TVK_USE_OBJLOADER  "Bundle tinyobjloader for .OBJ mesh loading"     OFF)

if(TVK_USE_NOTCURSES AND MSVC)
    message(WARNING "[TVK] TVK_USE_NOTCURSES requires MinGW/GCC/Clang. Disabling for MSVC.")
    set(TVK_USE_NOTCURSES OFF CACHE BOOL "" FORCE)
endif()

set(TVK_CORE_SOURCES
    src/tvk/TVK_Camera.cpp
    src/tvk/TVK_CommandBuffer.cpp
    src/tvk/TVK_JobSystem.cpp
    src/tvk/TVK_Math.cpp
    src/tvk/TVK_PAL.cpp
    src/tvk/TVK_Primitives.cpp
    src/tvk/TVK_Rasterizer.cpp
    src/tvk/TVK_Renderer.cpp
    src/tvk/TVK_TerminalPresenter.cpp
)

if(TVK_USE_NOTCURSES)
    list(APPEND TVK_CORE_SOURCES src/tvk/TVK_NotcursesPresenter.cpp)
endif()

add_library(tvk_core STATIC ${TVK_CORE_SOURCES})
target_include_directories(tvk_core PUBLIC include)

if(MSVC)
    target_compile_options(tvk_core PRIVATE
        /O2 /W4 /arch:AVX2
        /wd4324
        /wd4100
    )
else()

    target_compile_options(tvk_core PRIVATE
        -O3 -Wall -Wextra
        -march=native -mavx2 -mfma
        -fstrict-aliasing
        -fno-exceptions
    )

    target_link_options(tvk_core PUBLIC
        -static-libgcc
        -static-libstdc++
        -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic
    )

    if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        target_link_options(tvk_core PRIVATE -Wl,--large-address-aware)
    endif()
endif()

if(WIN32)
    target_link_libraries(tvk_core PRIVATE winmm)
endif()

find_package(Threads REQUIRED)
target_link_libraries(tvk_core PRIVATE Threads::Threads)

if(TVK_USE_NOTCURSES)
    include(ExternalProject)

    set(NC_SRC_DIR     "${CMAKE_SOURCE_DIR}/notcurses-master/notcurses-master")
    set(NC_BUILD_DIR   "${CMAKE_BINARY_DIR}/_notcurses_build")
    set(NC_INSTALL_DIR "${CMAKE_BINARY_DIR}/_notcurses_install")
    set(NC_LIB         "${NC_INSTALL_DIR}/lib/libnotcurses-core.a")

    ExternalProject_Add(notcurses_ext
        SOURCE_DIR   "${NC_SRC_DIR}"
        BINARY_DIR   "${NC_BUILD_DIR}"
        INSTALL_DIR  "${NC_INSTALL_DIR}"
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
            -DUSE_MULTIMEDIA=none
            -DUSE_DOCTEST=off
            -DUSE_PANDOC=off
            -DBUILD_EXECUTABLES=off
            -DBUILD_TESTING=off
            -DUSE_STATIC=on
            -DUSE_CXX=off
            -DUSE_POC=off
        BUILD_BYPRODUCTS "${NC_LIB}"
    )

    file(MAKE_DIRECTORY "${NC_INSTALL_DIR}/include")

    add_library(nc_core_lib STATIC IMPORTED GLOBAL)
    set_target_properties(nc_core_lib PROPERTIES
        IMPORTED_LOCATION             "${NC_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${NC_INSTALL_DIR}/include"
    )
    add_dependencies(nc_core_lib notcurses_ext)

    target_compile_definitions(tvk_core PUBLIC TVK_NOTCURSES_AVAILABLE)
    target_include_directories(tvk_core PRIVATE "${NC_INSTALL_DIR}/include")
    target_link_libraries(tvk_core PRIVATE nc_core_lib)

    if(WIN32)

        target_link_libraries(tvk_core PRIVATE unistring ncursesw systre tre intl iconv pcre2-posix pcre2-8 secur32 ws2_32 wsock32 deflate)
    else()
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(UNISTRING libunistring)
            if(UNISTRING_FOUND)
                target_link_libraries(tvk_core PRIVATE ${UNISTRING_LIBRARIES})
                target_include_directories(tvk_core PRIVATE ${UNISTRING_INCLUDE_DIRS})
            else()
                target_link_libraries(tvk_core PRIVATE unistring)
            endif()
        else()
            target_link_libraries(tvk_core PRIVATE unistring)
        endif()
    endif()

    message(STATUS "[TVK] notcurses backend: ENABLED (building from bundled source)")
    message(STATUS "[TVK]   Source  : ${NC_SRC_DIR}")
    message(STATUS "[TVK]   Install : ${NC_INSTALL_DIR}")
endif()

if(TVK_USE_OBJLOADER)
    include(FetchContent)
    FetchContent_Declare(
        tinyobjloader
        GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
        GIT_TAG        v2.0.0rc13
        GIT_SHALLOW    ON
    )
    FetchContent_MakeAvailable(tinyobjloader)

    target_compile_definitions(tvk_core PUBLIC TVK_OBJ_LOADER_AVAILABLE)
    target_link_libraries(tvk_core PRIVATE tinyobjloader::tinyobjloader)
    message(STATUS "[TVK] tinyobjloader: ENABLED (v2.0.0rc13)")
endif()

add_executable(sandbox examples/Sandbox.cpp)
target_link_libraries(sandbox PRIVATE tvk_core)

add_executable(asteroids examples/Asteroids_Demo.cpp)
target_link_libraries(asteroids PRIVATE tvk_core)

if(MSVC)
    target_compile_options(sandbox PRIVATE /O2 /arch:AVX2)
    target_compile_options(asteroids PRIVATE /O2 /arch:AVX2)
else()
    target_compile_options(sandbox PRIVATE -O3 -march=native -mavx2 -mfma)
    target_compile_options(asteroids PRIVATE -O3 -march=native -mavx2 -mfma)
    target_link_options(sandbox PRIVATE
        -static-libgcc
        -static-libstdc++
        -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic
    )
    target_link_options(asteroids PRIVATE
        -static-libgcc
        -static-libstdc++
        -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic
    )
endif()

message(STATUS "")
message(STATUS "╔══════════════════════════════════════╗")
message(STATUS "║     TVK Engine Build Configuration   ║")
message(STATUS "╠══════════════════════════════════════╣")
message(STATUS "║  Compiler    : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "║  Build type  : ${CMAKE_BUILD_TYPE}")
message(STATUS "║  Notcurses   : ${TVK_USE_NOTCURSES}")
message(STATUS "║  OBJ loader  : ${TVK_USE_OBJLOADER}")
message(STATUS "╚══════════════════════════════════════╝")
message(STATUS "")
