/**
 * @file
 * @brief SHORT TERM dummy TLS RNG for HTTPS Client Temp altcp TLS on SI917.
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 */

#ifndef HTTPS_CLIENT_TEMP_ALTCP_TLS_RNG_H
#define HTTPS_CLIENT_TEMP_ALTCP_TLS_RNG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int https_client_temp_dummy_rng(void * ctx, unsigned char * buffer, size_t len);

#ifdef __cplusplus
}
#endif

#endif // HTTPS_CLIENT_TEMP_ALTCP_TLS_RNG_H
