# wxl-core: the WarcraftXL runtime, one source tree built twice. WarcraftXL.dll for the stock
# Wow.exe 3.3.5a (32-bit, pulled into the client by an import the patcher adds) and WarcraftXL.dll
# for the recompiled client (64-bit, linked by the client and started through WarcraftXLInit).
# The mod host comes from the SDK checkout next door and is built here once per architecture.
# The patcher, MinHook and the two mods under mods/ carry client addresses and exist on x86 only.
# Copyright (C) 2026 WarcraftXL. GPLv3 (see source headers).
#
# One CMake tree is one architecture, so this file plays two roles depending on WXL_CORE_ARCH:
#
#   - Unset (`cmake -S . -B build`): a superbuild. cmake/superbuild.cmake configures and builds
#     this directory once per architecture as a child project, each with the matching Visual
#     Studio platform, and installs what each produces into one folder:
#
#       build/dist/bin/x86/WarcraftXL.dll
#       build/dist/bin/x86/wxl-core-patcher.exe
#       build/dist/bin/x64/WarcraftXL.dll
#       build/dist/Mods/<id>/{mod.json,bin/x86/<name>.dll}
#
#   - win-x86 or win-x64 (a child, or a tree configured by hand with the matching -A Win32 or
#     -A x64): the real targets of that one architecture, in build/<Config>/.

cmake_minimum_required(VERSION 3.21)
project(wxl-core VERSION 0.2.0 LANGUAGES NONE)

set(WXL_CORE_ARCH "" CACHE STRING
    "Architecture of this build tree (win-x86 or win-x64). Set by the superbuild; empty builds every target.")
option(WXL_CORE_MODS "Build the mods under mods/ (x86 only: both require wxl.patch)" ON)

set(WXL_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sdk" CACHE PATH "Path to a checkout of WarcraftXL/sdk")
if(NOT EXISTS "${WXL_SDK_DIR}/include/wxl/abi.h")
    message(FATAL_ERROR "WXL_SDK_DIR (${WXL_SDK_DIR}) is not a WarcraftXL/sdk checkout: "
        "include/wxl/abi.h is missing. Pass -DWXL_SDK_DIR=<path to WarcraftXL/sdk>.")
endif()

if(NOT WXL_CORE_ARCH)
    include(cmake/superbuild.cmake)
    return()
endif()

# --- One architecture from here on ---------------------------------------------------------------
if(WXL_CORE_ARCH STREQUAL "win-x86")
    set(WXL_CORE_X86 ON)
    set(WXL_CORE_ARCH_DIR x86)
    set(WXL_CORE_POINTER_SIZE 4)
elseif(WXL_CORE_ARCH STREQUAL "win-x64")
    set(WXL_CORE_X86 OFF)
    set(WXL_CORE_ARCH_DIR x64)
    set(WXL_CORE_POINTER_SIZE 8)
else()
    message(FATAL_ERROR "WXL_CORE_ARCH is '${WXL_CORE_ARCH}'; it must be win-x86 or win-x64.")
endif()

# Static CRT: the DLL loads inside the player's Wow.exe, which has no reason to carry the toolset's
# VC++ redistributable. A dynamic CRT turns a missing redist into a loader failure at process start
# (0xc0000142) with no log at all. Set before any language is enabled so every target, the SDK's
# host library included, matches.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

enable_language(CXX)
if(WXL_CORE_X86)
    enable_language(C) # MinHook
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL WXL_CORE_POINTER_SIZE)
    math(EXPR have "${CMAKE_SIZEOF_VOID_P} * 8")
    math(EXPR want "${WXL_CORE_POINTER_SIZE} * 8")
    message(FATAL_ERROR "this tree is WXL_CORE_ARCH=${WXL_CORE_ARCH} (${want}-bit) but the compiler "
        "targets ${have}-bit. With a Visual Studio generator pass the matching platform "
        "(-A Win32 for win-x86, -A x64 for win-x64); with another generator, configure from a "
        "developer prompt of the right architecture.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Every binary lands at <this tree>/<Config>/ whatever folder declares it; the current binary
# directory rather than the top one, so that a client may add this directory as its own
# subdirectory and build the DLL it links.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

# --- The SDK: the contract and the shared mod host -------------------------------------------------
set(WXL_SDK_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${WXL_SDK_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/sdk" EXCLUDE_FROM_ALL)

# A single integer a mod can read out of wxl_runtime_info.version: major*10000 + minor*100 + patch.
math(EXPR WXL_CORE_VERSION
    "${PROJECT_VERSION_MAJOR} * 10000 + ${PROJECT_VERSION_MINOR} * 100 + ${PROJECT_VERSION_PATCH}")

set(WXL_CORE_DEFS WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)

# --- WarcraftXL.dll --------------------------------------------------------------------------------
# What both architectures share: the log, the start-up, the mod host behind it, and the bodies
# of the seams (one file per domain of include/wxl/seams/). Each architecture adds its entry
# point and its attachment to the client below: the detours that run those bodies on x86, the
# exports the client calls on x64.
add_library(wxl-core SHARED
    src/runtime/Core.cpp
    src/common/Log.cpp
    src/seams/World.cpp)
set_target_properties(wxl-core PROPERTIES OUTPUT_NAME "WarcraftXL" PREFIX "")
target_include_directories(wxl-core PRIVATE src include)
target_link_libraries(wxl-core PRIVATE wxl::host)
target_compile_definitions(wxl-core PRIVATE
    ${WXL_CORE_DEFS} WXL_CORE_VERSION=${WXL_CORE_VERSION} WXL_CORE_BUILD)

install(TARGETS wxl-core RUNTIME DESTINATION "bin/${WXL_CORE_ARCH_DIR}" COMPONENT core)
install(FILES "$<TARGET_PDB_FILE:wxl-core>" DESTINATION "bin/${WXL_CORE_ARCH_DIR}"
    COMPONENT core OPTIONAL)

if(WXL_CORE_X86)
    # --- MinHook (BSD-2-Clause, deps/minhook, its own license file) --------------------------------
    # Declared here rather than through its own CMakeLists, which predates the CMake in use.
    # Third-party code: built at the toolset's default warning level, not ours.
    add_library(minhook STATIC
        deps/minhook/src/buffer.c
        deps/minhook/src/hook.c
        deps/minhook/src/trampoline.c
        deps/minhook/src/hde/hde32.c)
    target_include_directories(minhook PUBLIC deps/minhook/include)
    target_include_directories(minhook PRIVATE deps/minhook/src deps/minhook/src/hde)

    # The stock client is a shipped binary: the DLL reaches it by an import and an entry
    # trampoline the patcher adds, detours at resolved addresses, and byte patches. Every file
    # here holds an address, reads the image's headers, or serves a service built on those.
    target_sources(wxl-core PRIVATE
        src/runtime/Main.cpp
        src/runtime/AttachmentX86.cpp
        src/runtime/Image.cpp
        src/seams/AttachX86.cpp
        src/hook/Engine.cpp
        src/hook/Points.cpp
        src/services/HookService.cpp
        src/services/PatchService.cpp)
    target_link_libraries(wxl-core PRIVATE minhook)

    # --- wxl-core-patcher.exe ----------------------------------------------------------------------
    add_executable(wxl-core-patcher
        src/patcher/Main.cpp
        src/patcher/PeImage.cpp
        src/patcher/PatchScript.cpp
        src/common/Log.cpp)
    set_target_properties(wxl-core-patcher PROPERTIES OUTPUT_NAME "wxl-core-patcher")
    target_include_directories(wxl-core-patcher PRIVATE src)
    target_compile_definitions(wxl-core-patcher PRIVATE ${WXL_CORE_DEFS})

    install(TARGETS wxl-core-patcher RUNTIME DESTINATION bin/x86 COMPONENT core)
    install(FILES "$<TARGET_PDB_FILE:wxl-core-patcher>" DESTINATION bin/x86 COMPONENT core OPTIONAL)

    set(WXL_CORE_WARNED_TARGETS wxl-core wxl-core-patcher)
else()
    # The recompiled client links the DLL and calls WarcraftXLInit. Nothing is detoured and no
    # address exists: the seams are calls the client's own source makes.
    target_sources(wxl-core PRIVATE
        src/runtime/Init.cpp
        src/runtime/AttachmentX64.cpp
        src/seams/Table.cpp)

    # The import library, for the client to link; the headers it needs are under include/.
    install(TARGETS wxl-core ARCHIVE DESTINATION "lib/${WXL_CORE_ARCH_DIR}" COMPONENT core)

    set(WXL_CORE_WARNED_TARGETS wxl-core)
endif()

foreach(target IN LISTS WXL_CORE_WARNED_TARGETS)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
endforeach()

# --- The mods this runtime ships -------------------------------------------------------------------
if(WXL_CORE_MODS AND WXL_CORE_X86)
    add_subdirectory(mods)
elseif(WXL_CORE_MODS)
    message(STATUS "wxl-core: mods skipped on ${WXL_CORE_ARCH} (both are win-x86 only)")
endif()
