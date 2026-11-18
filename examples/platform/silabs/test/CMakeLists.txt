cmake_minimum_required(VERSION 3.13)

# mmic_host built with the camera test (mmic_host <transport args> --test-camera).
project(mmic_host_test C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

if(NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin"))
    message(FATAL_ERROR "mmic host test build only supports Linux and macOS (got ${CMAKE_SYSTEM_NAME}).")
endif()

option(MMIC_USE_CPC "Use Silicon Labs CPC instead of UART" OFF)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

set(MMIC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../mmic)

add_executable(mmic_host
    ${MMIC_DIR}/host_main.cpp
    ${MMIC_DIR}/mmic.cpp
    ${MMIC_DIR}/mmic_cluster_names.cpp
    ${MMIC_DIR}/chip_tool_storage.cpp
    ${MMIC_DIR}/matter_cert_issuer.cpp
    mmic_camera_test.cpp
)

target_include_directories(mmic_host PRIVATE ${MMIC_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(mmic_host PRIVATE MMIC_CAMERA_TEST=1)
target_compile_options(mmic_host PRIVATE -Wall -Wextra)
target_link_libraries(mmic_host PRIVATE Threads::Threads OpenSSL::Crypto)

if(MMIC_USE_CPC)
    find_path(CPC_INCLUDE_DIR NAMES sl_cpc.h)
    find_library(CPC_LIBRARY NAMES cpc)
    if(NOT CPC_INCLUDE_DIR OR NOT CPC_LIBRARY)
        message(FATAL_ERROR
            "MMIC_USE_CPC=ON but libcpc / sl_cpc.h were not found. "
            "Point the build at them with -DCPC_INCLUDE_DIR=... -DCPC_LIBRARY=...")
    endif()
    target_compile_definitions(mmic_host PRIVATE MMIC_USE_CPC=1)
    target_include_directories(mmic_host PRIVATE ${CPC_INCLUDE_DIR})
    target_link_libraries(mmic_host PRIVATE ${CPC_LIBRARY})
    message(STATUS "mmic_host test: CPC transport")
else()
    message(STATUS "mmic_host test: UART transport")
endif()
