/**
 * @file
 * @brief HTTPS Client Temp public API.
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 */

#ifndef __HTTPS_CLIENT_TEMP_H
#define __HTTPS_CLIENT_TEMP_H

#include <stddef.h>
#include <stdint.h>

#include "HttpsClientTempConfig.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    HTTPS_CLIENT_TEMP_OK = 0,
    HTTPS_CLIENT_TEMP_ERR_INVAL,
    HTTPS_CLIENT_TEMP_ERR_MEM,
    HTTPS_CLIENT_TEMP_ERR_FAIL,
    HTTPS_CLIENT_TEMP_ERR_BUSY,
} httpsClientTemp_err_t;

typedef struct
{
    const char * host;
    const char * uri;
    uint16_t port;
} httpsClientTemp_request_t;

typedef void (*httpsClientTemp_ready_cb)(void);

httpsClientTemp_err_t HttpsClientTempInit(httpsClientTemp_ready_cb ready_cb);

httpsClientTemp_err_t HttpsClientTempRequestGet(const httpsClientTemp_request_t * request);

#ifdef __cplusplus
}
#endif

#endif // __HTTPS_CLIENT_TEMP_H
