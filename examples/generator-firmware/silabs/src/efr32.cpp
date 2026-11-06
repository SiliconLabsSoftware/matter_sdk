/***************************************************************************
 * @file
 * @brief NVM3 definition of the default data
 *structures.
 *******************************************************************************
 * # License
 * <b>Copyright 2018 Silicon Laboratories Inc.
 *www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon
 *Laboratories Inc.  Your use of this software is
 *governed by the terms of Silicon Labs Master
 *Software License Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement.
 *This software is distributed to you in Source Code
 *format and is governed by the sections of the MSLA
 *applicable to Source Code.
 *
 ******************************************************************************/

#include "app.h"
#include <cstdint>
#include <em_device.h>
#include <platform/silabs/platformAbstraction/SilabsPlatform.h>

#if !defined(SL_TRUSTZONE_NONSECURE)
// Currently Series 3 is using a static nvm3 section placement determined by the linker script
// using nvm3_default_common_linker.c
#ifndef _SILICON_LABS_32B_SERIES_3
extern "C" {
#include <nvm3.h>
#include <nvm3_default_config.h>
#include <nvm3_generic.h>
#include <nvm3_hal_flash.h>
#if defined(NVM3_SECURITY)
#include <nvm3_hal_crypto_handle.h>
#endif
}

static nvm3_Handle_t nvm3_defaultHandleData;

#if (NVM3_DEFAULT_CACHE_SIZE != 0)
static nvm3_CacheEntry_t defaultCache[NVM3_DEFAULT_CACHE_SIZE];
#endif

static nvm3_Init_t nvm3_defaultInitData = {
    (nvm3_HalPtr_t) 0,
    0,
#if (NVM3_DEFAULT_CACHE_SIZE != 0)
    defaultCache,
#else
    NULL,
#endif
    NVM3_DEFAULT_CACHE_SIZE,
    NVM3_DEFAULT_MAX_OBJECT_SIZE,
    NVM3_DEFAULT_REPACK_HEADROOM,
    &nvm3_halFlashHandle,
#if defined(NVM3_SECURITY)
    &nvm3_halCryptoHandle,
    NVM3_DEFAULT_SECURITY_TYPE,
#endif
};

//------------------------------------------------------------------------------
// Public
//------------------------------------------------------------------------------

nvm3_Handle_t * nvm3_defaultHandle = &nvm3_defaultHandleData;

nvm3_Init_t * nvm3_defaultInit = &nvm3_defaultInitData;

sl_status_t nvm3_initDefault(void)
{
    return nvm3_open(nvm3_defaultHandle, nvm3_defaultInit);
}

sl_status_t nvm3_deinitDefault(void)
{
    return nvm3_close(nvm3_defaultHandle);
}
#endif // _SILICON_LABS_32B_SERIES_3

void setNvm3End(uint32_t end_addr)
{
#ifndef _SILICON_LABS_32B_SERIES_3
    uint32_t size             = NVM3_DEFAULT_NVM_SIZE;
    uint32_t nvm3_start_addr  = (end_addr - size);
    nvm3_defaultInit->nvmAdr  = (nvm3_HalPtr_t) nvm3_start_addr;
    nvm3_defaultInit->nvmSize = size;
    nvm3_initDefault();
#endif // _SILICON_LABS_32B_SERIES_3
}

#endif // !defined(SL_TRUSTZONE_NONSECURE)

/*
 * This block is necessary because the SPAM abstraction expects an implementation of the NvmInit and VerifyIfUpdated functions.
 * For the GFW, we don't need any specific execution to be done.
 * Sample apps use this to call the MigrationManager which isn't necessary for the GFW.
 */
#include <platform/silabs/platformAbstraction/SilabsPlatform.h>
namespace chip {
namespace DeviceLayer {
namespace Silabs {

CHIP_ERROR SilabsPlatform::NvmInit()
{
    return CHIP_NO_ERROR;
}

CHIP_ERROR SilabsPlatform::VerifyIfUpdated()
{
    return CHIP_NO_ERROR;
}

} // namespace Silabs
} // namespace DeviceLayer
} // namespace chip

void app_platform_init() {}
