# The mods core ships: each one a folder with its own CMakeLists.txt, mod.json and src/, written
# against the SDK like any other mod and buildable on its own. Built from here they land ready to
# copy in build/<Config>/Mods/<id>/{mod.json,bin/x86/<name>.dll}, and `cmake --install` of the
# `core` component (what the superbuild runs) collects the same folder into <prefix>/Mods/<id>/.
# Copyright (C) 2026 WarcraftXL. GPLv3 (see source headers).

# This tree is the one architecture the client has; wxl_add_mod then declares a real library
# target instead of a per-architecture superbuild.
set(WXL_MOD_ARCH win-x86)
include("${WXL_SDK_DIR}/cmake/wxl_add_mod.cmake")

# Routes a mod's binary and manifest into the Mods/ layout a runtime scans. Called by the mod's
# own CMakeLists.txt, next to its target, when it is built from this tree.
#   wxl_core_mod(<target> <manifest id>)
function(wxl_core_mod target id)
    set(mod_dir "${CMAKE_BINARY_DIR}/$<CONFIG>/Mods/${id}")
    set_target_properties(${target} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${mod_dir}/bin/x86"
        CXX_STANDARD 20)
    target_include_directories(${target} PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_compile_definitions(${target} PRIVATE ${WXL_CORE_DEFS})
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/mod.json" "${mod_dir}/mod.json"
        COMMENT "${id}: mod.json -> Mods/${id}")
    install(TARGETS ${target} LIBRARY DESTINATION "Mods/${id}/bin/x86" COMPONENT core)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/mod.json" DESTINATION "Mods/${id}" COMPONENT core)
endfunction()

add_subdirectory(wxl.glue-unlock)
add_subdirectory(wxl.named-archives)
