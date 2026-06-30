cmake_minimum_required(VERSION 3.13)
project(mmic_host C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to a Debug build (with -g, no optimization) so the binary is
# immediately usable under gdb / lldb. Override with -DCMAKE_BUILD_TYPE=Release.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

if(NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin"))
    message(FATAL_ERROR "mmic host build only supports Linux and macOS (got ${CMAKE_SYSTEM_NAME}).")
endif()

find_package(Threads REQUIRED)

add_executable(mmic_host
    host_main.cpp
    mmic.cpp
)

target_include_directories(mmic_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_options(mmic_host PRIVATE -Wall -Wextra)

# Emit full DWARF debug info and disable frame-pointer omission so backtraces
# in gdb / lldb are reliable, regardless of build type.
target_compile_options(mmic_host PRIVATE
    $<$<CONFIG:Debug>:-O0 -g3 -ggdb -fno-omit-frame-pointer>
    $<$<CONFIG:RelWithDebInfo>:-g3 -ggdb -fno-omit-frame-pointer>
)

# Ensure the linker keeps debug sections (mainly relevant on some toolchains).
target_link_options(mmic_host PRIVATE
    $<$<CONFIG:Debug>:-g>
    $<$<CONFIG:RelWithDebInfo>:-g>
)

target_link_libraries(mmic_host PRIVATE Threads::Threads)
