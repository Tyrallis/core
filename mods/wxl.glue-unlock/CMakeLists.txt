cmake_minimum_required(VERSION 3.21)

# No language here: wxl_add_mod enables C++ in the per-architecture child build.
project(wxl-glue-unlock VERSION 1.0.0 LANGUAGES NONE)

# Standalone: the SDK checkout next to core. From core's own tree wxl_add_mod is already defined.
if(NOT COMMAND wxl_add_mod)
    set(WXL_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../sdk" CACHE PATH "Path to the WarcraftXL SDK")
    include("${WXL_SDK_DIR}/cmake/wxl_add_mod.cmake")
endif()

# win-x86 only: the sites are addresses in Wow.exe build 12340 and mean nothing elsewhere.
wxl_add_mod(glue-unlock
    SOURCES src/mod.cpp
    MANIFEST mod.json
    TARGETS win-x86)

# The sites resolve through core's offset tables at compile time, which needs C++20. From core's
# tree the binary and the manifest also go to build/<Config>/Mods/.
if(TARGET glue-unlock)
    target_include_directories(glue-unlock PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../include")
    set_target_properties(glue-unlock PROPERTIES CXX_STANDARD 20)
    if(COMMAND wxl_core_mod)
        wxl_core_mod(glue-unlock wxl.glue-unlock)
    endif()
endif()
