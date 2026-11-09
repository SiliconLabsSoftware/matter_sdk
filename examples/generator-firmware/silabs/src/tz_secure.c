/*******************************************************************************
 * @file tz_secure.c
 * @brief TrustZone secure flash interface
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inin. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inin. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/

#include "em_msc.h"
#include "gfw_tz_secure.h" // Header file with secure interface API
#include <arm_cmse.h>      // CMSE definitions

#include <nvm3.h>
#include <nvm3_generic.h>
#include <nvm3_hal_flash.h>

//------------------------------------------------------------------------------
// Raw Flash
//------------------------------------------------------------------------------

int GFW_Flash_Init()
{
    MSC_Init();
    return 0;
}

int GFW_Flash_ErasePage(uint32_t addr)
{
    MSC_ErasePage((uint32_t *) addr);
    return 0;
}

int GFW_Flash_WriteWord(uint32_t addr, const uint8_t * x, size_t size)
{
    MSC_WriteWord((uint32_t *) addr, x, size);
    return 0;
}
