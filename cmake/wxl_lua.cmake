# wxl.lua, the runtime of the Lua mods, shipped with this runtime: built from the SDK checkout for
# this tree's architecture and laid out as the mod folder a runtime scans,
#
#   <Mods>/wxl.lua/{mod.json,LICENSE-LuaJIT.txt,bin/<arch>/wxl-lua.dll}
#
# <Mods> is <this tree>/<Config>/Mods, beside the mods of mods/, or WXL_CORE_LUA_MODS when a tree
# that builds this one as a subdirectory keeps its Mods/ elsewhere. `cmake --install` of the `core`
# component puts the same folder in <prefix>/Mods/wxl.lua/, which is how dist/ gets it.
#
# A build tree of its own rather than a subdirectory: wxl.lua links the dynamic CRT, which LuaJIT is
# compiled for, and this tree links the static one into everything it builds, the SDK's host
# library included. LuaJIT comes the way sdk/lua/cmake/luajit.cmake says: fetched and built with
# msvcbuild.bat by the Visual Studio in use, or prebuilt for this architecture through
# WXL_LUAJIT_LIBRARY and WXL_LUAJIT_INCLUDE_DIR, which is how a tree without msvcbuild.bat gets one.
# Copyright (c) 2026 WarcraftXL. MIT (see LICENSE).

set(WXL_LUAJIT_LIBRARY "" CACHE FILEPATH
    "A prebuilt static LuaJIT (lua51.lib) for this tree's architecture, for wxl.lua")
set(WXL_LUAJIT_INCLUDE_DIR "" CACHE PATH
    "The directory of that LuaJIT's headers (luajit.h, lua.h, lualib.h, lauxlib.h)")
set(WXL_CORE_LUA_MODS "" CACHE PATH
    "The Mods folder wxl.lua is put in; empty for <this tree>/<Config>/Mods")

if(NOT EXISTS "${WXL_SDK_DIR}/lua/CMakeLists.txt")
    message(FATAL_ERROR "WXL_SDK_DIR (${WXL_SDK_DIR}) has no lua/CMakeLists.txt, so wxl.lua cannot be "
        "built from it. Point WXL_SDK_DIR at a checkout of WarcraftXL/sdk that has it, or leave "
        "wxl.lua out with -DWXL_CORE_LUA=OFF.")
endif()

if(WXL_CORE_LUA_MODS)
    set(_wxl_lua_mods "${WXL_CORE_LUA_MODS}")
else()
    set(_wxl_lua_mods "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/Mods")
endif()

# What the child must see as this tree does. The types are not regenerated: that writes into the
# SDK checkout and runs a Windows tool, neither of which a runtime's build has a reason to do.
set(_wxl_lua_args
    "-DWXL_MOD_ARCH:STRING=${WXL_CORE_ARCH}"
    "-DWXL_SDK_DIR:PATH=${WXL_SDK_DIR}"
    "-DWXL_LUA_TYPES:BOOL=OFF")
foreach(var CMAKE_BUILD_TYPE CMAKE_TOOLCHAIN_FILE)
    if(${var})
        list(APPEND _wxl_lua_args "-D${var}=${${var}}")
    endif()
endforeach()
if(WXL_LUAJIT_LIBRARY OR WXL_LUAJIT_INCLUDE_DIR)
    list(APPEND _wxl_lua_args
        "-DWXL_LUAJIT_LIBRARY:FILEPATH=${WXL_LUAJIT_LIBRARY}"
        "-DWXL_LUAJIT_INCLUDE_DIR:PATH=${WXL_LUAJIT_INCLUDE_DIR}")
endif()

set(_wxl_lua_generator CMAKE_GENERATOR "${CMAKE_GENERATOR}")
foreach(var CMAKE_GENERATOR_PLATFORM CMAKE_GENERATOR_TOOLSET CMAKE_GENERATOR_INSTANCE)
    if(${var})
        list(APPEND _wxl_lua_generator ${var} "${${var}}")
    endif()
endforeach()

include(ExternalProject)

# BUILD_ALWAYS hands every build to the child, whose own dependency tracking decides whether
# anything is recompiled; its install step lays the mod folder out.
ExternalProject_Add(wxl-core-lua
    SOURCE_DIR  "${WXL_SDK_DIR}/lua"
    BINARY_DIR  "${CMAKE_CURRENT_BINARY_DIR}/wxl-lua"
    PREFIX      "${CMAKE_CURRENT_BINARY_DIR}/wxl-lua-steps"
    ${_wxl_lua_generator}
    CMAKE_ARGS  ${_wxl_lua_args}
    BUILD_COMMAND   "${CMAKE_COMMAND}" --build <BINARY_DIR> --config $<CONFIG>
    INSTALL_COMMAND "${CMAKE_COMMAND}" --install <BINARY_DIR> --config $<CONFIG>
                    --prefix "${_wxl_lua_mods}/wxl.lua"
    BUILD_ALWAYS ON)

# The same folder in an install of this tree: from the child's own install rules, into the prefix.
install(CODE "set(_wxl_lua_tree \"${CMAKE_CURRENT_BINARY_DIR}/wxl-lua\")" COMPONENT core)
install(CODE [==[
set(_wxl_lua_install --install "${_wxl_lua_tree}" --prefix "${CMAKE_INSTALL_PREFIX}/Mods/wxl.lua")
if(CMAKE_INSTALL_CONFIG_NAME)
    list(APPEND _wxl_lua_install --config "${CMAKE_INSTALL_CONFIG_NAME}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" ${_wxl_lua_install} RESULT_VARIABLE _wxl_lua_result)
if(NOT _wxl_lua_result EQUAL 0)
    message(FATAL_ERROR "wxl.lua could not be installed from ${_wxl_lua_tree}: build this tree first")
endif()
]==] COMPONENT core)

message(STATUS "wxl-core: wxl.lua (${WXL_CORE_ARCH}) -> ${_wxl_lua_mods}/wxl.lua")
