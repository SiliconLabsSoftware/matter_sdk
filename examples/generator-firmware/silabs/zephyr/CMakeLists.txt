# *******************************************************************************
# License
# <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
# *******************************************************************************
# The licensor of this software is Silicon Laboratories Inc.  Your use of this
# software is governed by the terms of Silicon Labs Master Software License
# agreement (MSLA) available at
# www.silabs.com/about-us/legal/master-software-license-agreement.  This
# software is distributed to you in Source Code format and is governed by the
# sections of the MSLA applicable to Source Code.
# *******************************************************************************

cmake_minimum_required(VERSION 3.20)

if(DEFINED CHIP_ROOT)
  get_filename_component(CHIP_ROOT ${CHIP_ROOT} REALPATH)
else()
  get_filename_component(CHIP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../.. REALPATH)
endif()

set(PROVISION_SUPPORT_DIR ${CHIP_ROOT}/third_party/silabs/matter_support/provision)

list(APPEND ZEPHYR_EXTRA_MODULES ${CHIP_ROOT}/config/silabs)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})

project(matter-provision-gfw)

include(${CHIP_ROOT}/config/silabs/app/enable-gnu-std.cmake)

target_compile_options(app PRIVATE -Werror -Wno-error=maybe-uninitialized)

set(PROV_SDK ${CHIP_ROOT}/examples/platform/silabs/provision)

if(NOT DEFINED PROVISION_LIBS_DIR)
  set(PROVISION_LIBS_DIR ${PROVISION_SUPPORT_DIR}/libs)
endif()

# Match SLC GFW / matter_sdk ABI selection: SiWx917 -> m4-softfp, EFR Series 2 -> m33-hard.
if(CONFIG_SOC_FAMILY_SILABS_SIWX91X)
  set(PROVISION_CORE_LIB ${PROVISION_LIBS_DIR}/libProvision-m4-softfp.a)
else()
  set(PROVISION_CORE_LIB ${PROVISION_LIBS_DIR}/libProvision-m33-hard.a)
endif()

if(NOT EXISTS ${PROVISION_CORE_LIB})
  message(FATAL_ERROR
    "Missing Provision Core archive: ${PROVISION_CORE_LIB}\n"
    "Check out the third_party/silabs/matter_support submodule "
    "(or pass -DPROVISION_LIBS_DIR=...).")
endif()

target_include_directories(app PRIVATE
    include
    ${PROVISION_SUPPORT_DIR}
    ${CHIP_ROOT}/examples
    ${CHIP_ROOT}/examples/platform/silabs
    ${CHIP_ROOT}/src/include
    ${CHIP_ROOT}/src
    ${CHIP_ROOT}/third_party/nlio/include
    ${CHIP_ROOT}/third_party/nlassert/include
    # EFR boards get these via the Silabs PSA driver module; SiWx917 GFW does not.
    ${ZEPHYR_HAL_SILABS_MODULE_DIR}/simplicity_sdk/platform/security/sl_component/sl_mbedtls_support/inc
    ${ZEPHYR_HAL_SILABS_MODULE_DIR}/simplicity_sdk/platform/security/sl_component/sl_psa_driver/inc
)

target_sources(app PRIVATE
    src/main.cpp
    ${PROV_SDK}/ProvisionCryptoPSA.cpp
    ${PROV_SDK}/ProvisionStorageReaderCommon.cpp
    ${PROV_SDK}/ProvisionStorageCustom.cpp
    ${PROV_SDK}/ProvisionChannelRTT.cpp
    ${PROV_SDK}/zephyr/ProvisionStorageReader.cpp
    ${PROV_SDK}/zephyr/ProvisionStorageWriter.cpp

    # Contains implementation of sl_psa_get_most_secure_key_location()
    # Silicon Labs Zephyr links the header file, but not the implementation as of 2026-08-14.
    #
    # Message from the Zephyr team:
    # "Since SiSDK for Zephyr is currently using Zephyr's fork of PSA Crypto, rather than the SiSDK
    # fork of PSA Crypto, we intentionally didn't include some of these SL extensions to PSA Crypto, 
    # since some of them rely on patches in PSA Crypto that the Zephyr fork doesn't have. That specific 
    # function seems safe to use, though, so we should probably include that file."
    # 
    # Adding it here until the Zephyr team includes the implementation in the Zephyr module.
    ${ZEPHYR_HAL_SILABS_MODULE_DIR}/simplicity_sdk/platform/security/sl_component/sl_mbedtls_support/src/sl_psa_crypto.c
)

target_link_libraries(app PRIVATE ${PROVISION_CORE_LIB})

target_compile_definitions(app PRIVATE
    SL_MATTER_PROVISION_CHANNEL_ENABLED=1
    SILABS_PROVISION_PROTOCOL_V1=0
    SL_PROVISION_GENERATOR=1
    CHIP_CONFIG_MEMORY_MGMT_MALLOC=1
    CHIP_DEVICE_LAYER_TARGET_ZEPHYR=1
)

# SiWx917 device headers do not define FLASH_PAGE_SIZE (EFR does). Match FreeRTOS
# SiWx917 SDK / DT erase-block-size (4096).
if(CONFIG_SOC_FAMILY_SILABS_SIWX91X)
    target_compile_definitions(app PRIVATE FLASH_PAGE_SIZE=4096)
endif()
