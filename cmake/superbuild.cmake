# The superbuild role of CMakeLists.txt: one child project per architecture, each configured from
# the same source directory with WXL_CORE_ARCH set and the matching Visual Studio platform, each
# installing its `core` component into one dist/ folder. Same shape as the SDK's wxl_add_mod.
# Copyright (C) 2026 WarcraftXL. GPLv3 (see source headers).

set(WXL_CORE_TARGETS "win-x86;win-x64" CACHE STRING "Architectures to build (win-x86;win-x64).")
set(WXL_CORE_DIST "${CMAKE_BINARY_DIR}/dist" CACHE PATH "Where every architecture's output is collected.")

set(_WXL_CORE_KNOWN_ARCHS win-x86 win-x64)
foreach(arch IN LISTS WXL_CORE_TARGETS)
    if(NOT arch IN_LIST _WXL_CORE_KNOWN_ARCHS)
        message(FATAL_ERROR "WXL_CORE_TARGETS has '${arch}'; the choices are: ${_WXL_CORE_KNOWN_ARCHS}")
    endif()
endforeach()
if(NOT WXL_CORE_TARGETS)
    message(FATAL_ERROR "WXL_CORE_TARGETS is empty; nothing to build.")
endif()

# Two architectures from one tree needs a generator that takes the platform as a parameter. Any
# other generator can still build the one architecture its environment is set up for.
set(is_vs FALSE)
if(CMAKE_GENERATOR MATCHES "^Visual Studio")
    set(is_vs TRUE)
endif()
list(LENGTH WXL_CORE_TARGETS arch_count)
if(NOT is_vs AND arch_count GREATER 1)
    message(FATAL_ERROR "building ${WXL_CORE_TARGETS} from one tree needs a Visual Studio generator, "
        "which selects the architecture per child project; '${CMAKE_GENERATOR}' cannot. Configure "
        "with -G \"Visual Studio 17 2022\" (or the version installed), or build one architecture from "
        "a matching developer prompt with -DWXL_CORE_TARGETS=win-x64 or win-x86.")
endif()

# win-x86 -> x86 (the folder under bin/), Win32 (the Visual Studio platform); win-x64 -> x64, x64.
function(_wxl_core_arch_facts arch out_dir out_platform)
    if(arch STREQUAL "win-x64")
        set(${out_dir} "x64" PARENT_SCOPE)
        set(${out_platform} "x64" PARENT_SCOPE)
    else()
        set(${out_dir} "x86" PARENT_SCOPE)
        set(${out_platform} "Win32" PARENT_SCOPE)
    endif()
endfunction()

# Fails before the child does, with the one message that matters, when the Visual Studio in use
# has no compiler for the architecture.
function(_wxl_core_check_toolchain arch dir)
    if(NOT CMAKE_GENERATOR MATCHES "^Visual Studio" OR NOT CMAKE_GENERATOR_INSTANCE)
        return()
    endif()
    string(REGEX REPLACE ",.*$" "" vs_root "${CMAKE_GENERATOR_INSTANCE}")
    file(GLOB found "${vs_root}/VC/Tools/MSVC/*/bin/Host*/${dir}/cl.exe")
    if(found)
        return()
    endif()
    message(FATAL_ERROR "the ${dir} MSVC toolchain is not installed in\n  ${vs_root}\nso ${arch} "
        "cannot be built. Install it: Visual Studio Installer > Modify > Individual components > "
        "\"MSVC v14x - VS 20xx C++ x64/x86 build tools (Latest)\" (one component carries both "
        "targets), then configure again. To build without it: -DWXL_CORE_TARGETS=<the others>.")
endfunction()

# Settings the children must see as this tree saw them.
set(forward)
foreach(var WXL_SDK_DIR WXL_CORE_MODS CMAKE_BUILD_TYPE)
    if(DEFINED ${var})
        list(APPEND forward "-D${var}=${${var}}")
    endif()
endforeach()
set(generator_args CMAKE_GENERATOR "${CMAKE_GENERATOR}")
if(CMAKE_GENERATOR_TOOLSET)
    list(APPEND generator_args CMAKE_GENERATOR_TOOLSET "${CMAKE_GENERATOR_TOOLSET}")
endif()
if(CMAKE_GENERATOR_INSTANCE)
    list(APPEND generator_args CMAKE_GENERATOR_INSTANCE "${CMAKE_GENERATOR_INSTANCE}")
endif()

include(ExternalProject)
set(children)

foreach(arch IN LISTS _WXL_CORE_KNOWN_ARCHS)
    _wxl_core_arch_facts("${arch}" dir platform)
    if(NOT arch IN_LIST WXL_CORE_TARGETS)
        message(STATUS "wxl-core: ${arch}  skipped (not in the targets: ${WXL_CORE_TARGETS})")
        continue()
    endif()
    _wxl_core_check_toolchain("${arch}" "${dir}")

    set(platform_args)
    if(is_vs)
        set(platform_args CMAKE_GENERATOR_PLATFORM "${platform}")
    endif()

    # BUILD_ALWAYS hands every build to the child, whose own dependency tracking decides whether
    # anything is recompiled; the configure step runs once and the child reconfigures itself when
    # its CMakeLists.txt changes. The install step takes the `core` component only: the mods'
    # own install rules from wxl_add_mod (bin/x86/<name>.dll) are not the dist/ layout.
    ExternalProject_Add(wxl-core-${arch}
        SOURCE_DIR  "${CMAKE_CURRENT_SOURCE_DIR}"
        BINARY_DIR  "${CMAKE_BINARY_DIR}/${arch}"
        PREFIX      "${CMAKE_BINARY_DIR}/${arch}-steps"
        INSTALL_DIR "${WXL_CORE_DIST}"
        ${generator_args}
        ${platform_args}
        CMAKE_ARGS
            "-DWXL_CORE_ARCH:STRING=${arch}"
            "-DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>"
            ${forward}
        INSTALL_COMMAND "${CMAKE_COMMAND}" --install <BINARY_DIR> --config $<CONFIG> --component core
        BUILD_ALWAYS ON
        BUILD_BYPRODUCTS "${WXL_CORE_DIST}/bin/${dir}/WarcraftXL.dll")
    list(APPEND children wxl-core-${arch})
    message(STATUS "wxl-core: ${arch}  -> ${WXL_CORE_DIST}/bin/${dir}/WarcraftXL.dll")
endforeach()

add_custom_target(wxl-core-dist ALL DEPENDS ${children})
message(STATUS "wxl-core: dist folder ${WXL_CORE_DIST}")
