cmake_minimum_required(VERSION 3.13)
project(mmic_host C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin"))
    message(FATAL_ERROR "mmic host build only supports Linux and macOS (got ${CMAKE_SYSTEM_NAME}).")
endif()

find_package(Threads REQUIRED)

add_executable(mmic_host
    host_main.c
    mmic.c
)

target_include_directories(mmic_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_options(mmic_host PRIVATE -Wall -Wextra)

target_link_libraries(mmic_host PRIVATE Threads::Threads)
