/**
 * @file
 * @brief Direct lwIP altcp TLS HTTPS client.
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 */

#ifndef HTTPS_CLIENT_ALTCP_H
#define HTTPS_CLIENT_ALTCP_H

#include <stdint.h>

#include "lwip/err.h"

#ifdef __cplusplus
extern "C" {
#endif

err_t https_client_get(const char * server_ip, uint16_t port, const char * host, const char * uri);

#ifdef __cplusplus
}
#endif

#endif // HTTPS_CLIENT_ALTCP_H
