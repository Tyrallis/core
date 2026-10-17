# wxl-core: the WarcraftXL runtime for the stock Wow.exe 3.3.5a. Two binaries, both 32-bit:
# WarcraftXL.dll, pulled into the client by an import the patcher adds, and wxl-core-patcher.exe,
# which adds it. The mod host comes from the SDK checkout next door and is built here once, as
# are the two mods under mods/ that carry the client's static byte patches.
# Copyright (C) 2026 WarcraftXL. GPLv3 (see source headers).

cmake_minimum_required(VERSION 3.21)
project(wxl-core VERSION 0.2.0 LANGUAGES C CXX)

# The client is a 32-bit process; a 64-bit configure builds nothing useful.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "wxl-core loads into the 32-bit client; configure with -A Win32.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT: the DLL loads inside the player's Wow.exe, which has no reason to carry the toolset's
# VC++ redistributable. A dynamic CRT turns a missing redist into a loader failure at process start
# (0xc0000142) with no log at all. Set before the SDK is added so its host library matches.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Every binary lands at build/<Config>/ whatever folder declares it.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# --- The SDK: the contract and the shared mod host -------------------------------------------------
set(WXL_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sdk" CACHE PATH "Path to a checkout of WarcraftXL/sdk")
if(NOT EXISTS "${WXL_SDK_DIR}/include/wxl/abi.h")
    message(FATAL_ERROR "WXL_SDK_DIR (${WXL_SDK_DIR}) is not a WarcraftXL/sdk checkout: "
        "include/wxl/abi.h is missing. Pass -DWXL_SDK_DIR=<path to WarcraftXL/sdk>.")
endif()
set(WXL_SDK_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${WXL_SDK_DIR}" "${CMAKE_BINARY_DIR}/sdk" EXCLUDE_FROM_ALL)

# --- MinHook (BSD-2-Clause, deps/minhook, its own license file) ------------------------------------
# Declared here rather than through its own CMakeLists, which predates the CMake in use. Third-party
# code: built at the toolset's default warning level, not ours.
add_library(minhook STATIC
    deps/minhook/src/buffer.c
    deps/minhook/src/hook.c
    deps/minhook/src/trampoline.c
    deps/minhook/src/hde/hde32.c)
target_include_directories(minhook PUBLIC deps/minhook/include)
target_include_directories(minhook PRIVATE deps/minhook/src deps/minhook/src/hde)

# A single integer a mod can read out of wxl_runtime_info.version: major*10000 + minor*100 + patch.
math(EXPR WXL_CORE_VERSION
    "${PROJECT_VERSION_MAJOR} * 10000 + ${PROJECT_VERSION_MINOR} * 100 + ${PROJECT_VERSION_PATCH}")

set(WXL_CORE_DEFS WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)

# --- WarcraftXL.dll --------------------------------------------------------------------------------
add_library(wxl-core SHARED
    src/runtime/Main.cpp
    src/runtime/Core.cpp
    src/runtime/Image.cpp
    src/runtime/WorldSeam.cpp
    src/hook/Engine.cpp
    src/hook/Points.cpp
    src/services/HookService.cpp
    src/services/PatchService.cpp
    src/common/Log.cpp)
set_target_properties(wxl-core PROPERTIES OUTPUT_NAME "WarcraftXL" PREFIX "")
target_include_directories(wxl-core PRIVATE src include)
target_link_libraries(wxl-core PRIVATE wxl::host minhook)
target_compile_definitions(wxl-core PRIVATE ${WXL_CORE_DEFS} WXL_CORE_VERSION=${WXL_CORE_VERSION})

# --- wxl-core-patcher.exe --------------------------------------------------------------------------
add_executable(wxl-core-patcher
    src/patcher/Main.cpp
    src/patcher/PeImage.cpp
    src/patcher/PatchScript.cpp
    src/common/Log.cpp)
set_target_properties(wxl-core-patcher PROPERTIES OUTPUT_NAME "wxl-core-patcher")
target_include_directories(wxl-core-patcher PRIVATE src)
target_compile_definitions(wxl-core-patcher PRIVATE ${WXL_CORE_DEFS})

foreach(target wxl-core wxl-core-patcher)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
endforeach()

# --- The mods this runtime ships -------------------------------------------------------------------
option(WXL_CORE_MODS "Build the mods under mods/ into build/<Config>/Mods/" ON)
if(WXL_CORE_MODS)
    add_subdirectory(mods)
endif()
