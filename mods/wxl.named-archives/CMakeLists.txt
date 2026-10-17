cmake_minimum_required(VERSION 3.21)

# No language here: wxl_add_mod enables C++ in the per-architecture child build.
project(wxl-named-archives VERSION 1.0.0 LANGUAGES NONE)

# Standalone: the SDK checkout next to core. From core's own tree wxl_add_mod is already defined.
if(NOT COMMAND wxl_add_mod)
    set(WXL_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../sdk" CACHE PATH "Path to the WarcraftXL SDK")
    include("${WXL_SDK_DIR}/cmake/wxl_add_mod.cmake")
endif()

# win-x86 only: the strings it edits live in Wow.exe build 12340 and nowhere else.
wxl_add_mod(named-archives
    SOURCES src/mod.cpp
    MANIFEST mod.json
    TARGETS win-x86)

# From core's tree the binary and the manifest also go to build/<Config>/Mods/.
if(TARGET named-archives AND COMMAND wxl_core_mod)
    wxl_core_mod(named-archives wxl.named-archives)
endif()
