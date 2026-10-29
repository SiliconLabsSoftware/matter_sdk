cmake_minimum_required(VERSION 3.18)
project(si91x_rcp_linux LANGUAGES C)

include(GNUInstallDirs)

option(SI91X_RCP_BUILD_KERNEL_MODULE "Build the si91x_rcp_eth kernel module" ON)
option(SI91X_RCP_BUILD_DAEMON "Build the si91x-rcpd address sharing daemon" ON)
set(SI91X_RCP_SYSCONF_DIR "/etc" CACHE PATH "System configuration directory")
set(SI91X_RCP_SYSTEMD_UNIT_DIR "/lib/systemd/system" CACHE PATH "systemd unit directory")

# Wire format shared with the SiWx917 firmware.
set(SI91X_RCP_PROTOCOL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

if(SI91X_RCP_BUILD_KERNEL_MODULE)
    add_subdirectory(kernel)
    install(FILES config/si91x-rcp.modprobe.conf
            DESTINATION "${SI91X_RCP_SYSCONF_DIR}/modprobe.d"
            RENAME si91x-rcp.conf)
endif()

if(SI91X_RCP_BUILD_DAEMON)
    add_subdirectory(daemon)

    configure_file(config/si91x-rcpd.service.in "${CMAKE_CURRENT_BINARY_DIR}/si91x-rcpd.service" @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/si91x-rcpd.service" DESTINATION "${SI91X_RCP_SYSTEMD_UNIT_DIR}")
    install(FILES config/90-si91x-rcp.sysctl.conf
            DESTINATION "${SI91X_RCP_SYSCONF_DIR}/sysctl.d"
            RENAME 90-si91x-rcp.conf)
    install(FILES config/99-si91x-rcp.networkmanager.conf
            DESTINATION "${SI91X_RCP_SYSCONF_DIR}/NetworkManager/conf.d"
            RENAME 99-si91x-rcp.conf)
endif()
