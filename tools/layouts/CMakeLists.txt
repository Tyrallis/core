# The layout dumper: one translation unit that includes the client fork's headers and prints
# sizeof / offsetof for every entry of layouts.txt. measure_layouts.py configures and builds this
# project; it is not meant to be configured by hand.
#
# The compile flags are NOT written here. The driver reads them out of the fork's root
# CMakeLists.txt (the global add_compile_definitions block, the Win32 branch's add_compile_options
# and add_compile_definitions, CMAKE_CXX_STANDARD) and passes them in as the three cache variables
# below, so the dumper is compiled exactly as the fork compiles its own Win32 build. The driver
# also refuses to run with anything but a 32-bit MSVC toolchain in the environment.

cmake_minimum_required(VERSION 3.10)

project(wxl_layouts CXX)

if (NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "The layout dumper must be built 32-bit (pointer size is ${CMAKE_SIZEOF_VOID_P}).")
endif ()
if (NOT MSVC)
    message(FATAL_ERROR "The layout dumper must be built with MSVC, like the fork.")
endif ()

set(WXL_CLIENT_DIR "" CACHE PATH "The client fork checkout (read-only input)")
set(WXL_DUMPER_SOURCE "" CACHE FILEPATH "The generated dumper translation unit")
set(WXL_FORK_CXX_STANDARD "" CACHE STRING "CMAKE_CXX_STANDARD as set by the fork")
set(WXL_FORK_DEFINES "" CACHE STRING "add_compile_definitions as the fork applies them to Win32 (semicolon list)")
set(WXL_FORK_OPTIONS "" CACHE STRING "add_compile_options as the fork applies them to Win32 (semicolon list)")
set(WXL_FORK_INCLUDES "" CACHE STRING "include directories, relative to the checkout (semicolon list)")

foreach (var WXL_CLIENT_DIR WXL_DUMPER_SOURCE WXL_FORK_CXX_STANDARD)
    if ("${${var}}" STREQUAL "")
        message(FATAL_ERROR "${var} must be set; run measure_layouts.py instead of configuring by hand.")
    endif ()
endforeach ()

set(CMAKE_CXX_STANDARD ${WXL_FORK_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(${WXL_FORK_DEFINES})
add_compile_options(${WXL_FORK_OPTIONS})

add_executable(layout_dumper ${WXL_DUMPER_SOURCE})

target_include_directories(layout_dumper PRIVATE ${WXL_CLIENT_DIR})
foreach (dir ${WXL_FORK_INCLUDES})
    target_include_directories(layout_dumper PRIVATE ${WXL_CLIENT_DIR}/${dir})
endforeach ()

# offsetof on a class with a base or a virtual function is conditionally supported; MSVC accepts
# it (C4515 or C4815 are never raised for it), the warning here is only for the Win32 size_t format.
target_compile_options(layout_dumper PRIVATE /W3 /wd4477)
