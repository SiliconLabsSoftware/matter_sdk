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
