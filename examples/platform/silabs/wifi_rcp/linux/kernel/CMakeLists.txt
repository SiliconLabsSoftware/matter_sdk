# Kbuild cannot be driven natively by CMake: sources are staged in the build tree and
# the kernel build system is invoked on them.

execute_process(COMMAND uname -r OUTPUT_VARIABLE _host_kernel_release OUTPUT_STRIP_TRAILING_WHITESPACE)

set(SI91X_RCP_KERNEL_RELEASE "${_host_kernel_release}" CACHE STRING "Target kernel release")
set(SI91X_RCP_KERNEL_DIR "/lib/modules/${SI91X_RCP_KERNEL_RELEASE}/build" CACHE PATH "Kernel build tree")
set(SI91X_RCP_KERNEL_ARCH "" CACHE STRING "ARCH passed to Kbuild when cross compiling")
set(SI91X_RCP_KERNEL_CROSS_COMPILE "" CACHE STRING "CROSS_COMPILE passed to Kbuild when cross compiling")
set(SI91X_RCP_MODULE_INSTALL_DIR "/lib/modules/${SI91X_RCP_KERNEL_RELEASE}/extra" CACHE PATH "Module install directory")
set(SI91X_RCP_SDIO_VENDOR_ID "0x041B" CACHE STRING "SDIO vendor ID reported by the SiWx917")
set(SI91X_RCP_SDIO_DEVICE_ID "0x0917" CACHE STRING "SDIO device ID reported by the SiWx917")

if(NOT EXISTS "${SI91X_RCP_KERNEL_DIR}/Makefile")
    message(FATAL_ERROR "Kernel build tree not found at ${SI91X_RCP_KERNEL_DIR}. "
                        "Install the kernel headers or set SI91X_RCP_KERNEL_DIR.")
endif()

find_program(MAKE_EXECUTABLE NAMES make gmake REQUIRED)

set(_kbuild_dir "${CMAKE_CURRENT_BINARY_DIR}/kbuild")
set(_module "${_kbuild_dir}/si91x_rcp_eth.ko")
set(_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/Kbuild"
    "${CMAKE_CURRENT_SOURCE_DIR}/si91x_rcp_eth.c"
    "${SI91X_RCP_PROTOCOL_DIR}/wifi_rcp_protocol.h")

set(_kbuild_args
    -C "${SI91X_RCP_KERNEL_DIR}"
    "M=${_kbuild_dir}"
    "SI91X_RCP_SDIO_VENDOR_ID=${SI91X_RCP_SDIO_VENDOR_ID}"
    "SI91X_RCP_SDIO_DEVICE_ID=${SI91X_RCP_SDIO_DEVICE_ID}")
if(SI91X_RCP_KERNEL_ARCH)
    list(APPEND _kbuild_args "ARCH=${SI91X_RCP_KERNEL_ARCH}")
endif()
if(SI91X_RCP_KERNEL_CROSS_COMPILE)
    list(APPEND _kbuild_args "CROSS_COMPILE=${SI91X_RCP_KERNEL_CROSS_COMPILE}")
endif()

add_custom_command(
    OUTPUT "${_module}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_kbuild_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${_sources} "${_kbuild_dir}"
    COMMAND "${MAKE_EXECUTABLE}" ${_kbuild_args} modules
    DEPENDS ${_sources}
    COMMENT "Building si91x_rcp_eth.ko against ${SI91X_RCP_KERNEL_DIR}"
    VERBATIM)

add_custom_target(si91x_rcp_eth ALL DEPENDS "${_module}")

install(FILES "${_module}" DESTINATION "${SI91X_RCP_MODULE_INSTALL_DIR}")
install(CODE "
    if(\"\$ENV{DESTDIR}\" STREQUAL \"\")
        execute_process(COMMAND depmod -a ${SI91X_RCP_KERNEL_RELEASE})
    endif()
")
