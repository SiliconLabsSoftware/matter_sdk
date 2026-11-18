cmake_minimum_required(VERSION 3.16)

# camera_host: camera part of the Silabs camera-app, driven by the SoC over MMIC.
project(camera_host LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "camera_host only supports Linux (got ${CMAKE_SYSTEM_NAME}).")
endif()

get_filename_component(MATTER_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
set(MMIC_DIR "${MATTER_SDK_ROOT}/examples/platform/silabs/mmic")

option(CAMERA_HOST_USE_SYSTEM_LIBDATACHANNEL "Use an installed libdatachannel instead of the Matter SDK submodule" OFF)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0)

if(CAMERA_HOST_USE_SYSTEM_LIBDATACHANNEL)
    find_package(LibDataChannel REQUIRED)
    set(LIBDATACHANNEL_TARGET LibDataChannel::LibDataChannel)
else()
    set(LIBDATACHANNEL_DIR "${MATTER_SDK_ROOT}/third_party/libdatachannel/repo")
    if(NOT EXISTS "${LIBDATACHANNEL_DIR}/CMakeLists.txt" OR NOT EXISTS "${LIBDATACHANNEL_DIR}/deps/libjuice/CMakeLists.txt")
        message(FATAL_ERROR
            "libdatachannel sources not found in ${LIBDATACHANNEL_DIR}. From the Matter SDK root, run:\n"
            "  git submodule update --init --recursive third_party/libdatachannel/repo\n"
            "or configure with -DCAMERA_HOST_USE_SYSTEM_LIBDATACHANNEL=ON.")
    endif()
    # Media transport only, same options as the Matter SDK build.
    set(NO_WEBSOCKET ON CACHE BOOL "" FORCE)
    set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
    set(NO_TESTS ON CACHE BOOL "" FORCE)
    set(USE_GNUTLS OFF CACHE BOOL "" FORCE)
    set(USE_NICE OFF CACHE BOOL "" FORCE)
    add_subdirectory("${LIBDATACHANNEL_DIR}" libdatachannel EXCLUDE_FROM_ALL)
    set(LIBDATACHANNEL_TARGET LibDataChannel::LibDataChannelStatic)
endif()

add_executable(camera_host
    src/main.cpp
    src/CameraDevice.cpp
    src/CameraHost.cpp
    src/MmicLink.cpp
    src/Tlv.cpp
    src/WebRtcManager.cpp
    # MMIC framing and cluster names, shared with the SoC and mmic_host.
    ${MMIC_DIR}/mmic.cpp
    ${MMIC_DIR}/mmic_cluster_names.cpp
)

target_include_directories(camera_host PRIVATE src ${MMIC_DIR})
target_compile_options(camera_host PRIVATE -Wall -Wextra)
target_link_libraries(camera_host PRIVATE PkgConfig::GSTREAMER ${LIBDATACHANNEL_TARGET} Threads::Threads)

install(TARGETS camera_host RUNTIME DESTINATION bin)
