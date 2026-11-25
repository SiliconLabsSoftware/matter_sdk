/*******************************************************************************
 * @file gfw_tz_secure.h
 * @brief TrustZone secure flash interface header.
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

#ifndef GFW_TRUSTZONE_SECURE
#define GFW_TRUSTZONE_SECURE

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#pragma once
extern "C" {
#endif

extern int GFW_Flash_Init() __attribute__((cmse_nonsecure_entry));
extern int GFW_Flash_ErasePage(uint32_t addr) __attribute__((cmse_nonsecure_entry));
extern int GFW_Flash_WriteWord(uint32_t addr, const uint8_t * x, size_t size) __attribute__((cmse_nonsecure_entry));

#ifdef __cplusplus
}
#endif

#endif // GFW_TRUSTZONE_SECURE
