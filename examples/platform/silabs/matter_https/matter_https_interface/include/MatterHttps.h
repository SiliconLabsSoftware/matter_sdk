/**
 * @file
 * @brief Matter abstraction layer for HTTPS client.
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 */

#ifndef __MATTER_HTTPS_H
#define __MATTER_HTTPS_H

#include <stddef.h>
#include <stdint.h>

#include "MatterHttpsConfig.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    MATTER_HTTPS_OK = 0,
    MATTER_HTTPS_ERR_INVAL,
    MATTER_HTTPS_ERR_MEM,
    MATTER_HTTPS_ERR_FAIL,
    MATTER_HTTPS_ERR_BUSY,
} matterHttps_err_t;

typedef struct
{
    const char * host; /* IP string (default) or hostname when MATTER_HTTPS_USE_DNS=1 */
    const char * uri;
    uint16_t port;
} matterHttps_request_t;

matterHttps_err_t MatterHttpsInit(const char * rootCaPem, size_t rootCaPemLen);

matterHttps_err_t MatterHttpsRequestGet(const matterHttps_request_t * request);

#ifdef __cplusplus
}
#endif

#endif // __MATTER_HTTPS_H
