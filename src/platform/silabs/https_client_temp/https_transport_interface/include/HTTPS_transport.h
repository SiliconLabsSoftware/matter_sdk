/**
 * @file
 * @brief HTTPS transport abstraction for HTTPS Client Temp (forked altcp TLS).
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 */

#ifndef HTTPS_TRANSPORT_H
#define HTTPS_TRANSPORT_H

#include "FreeRTOS.h"
#include "event_groups.h"
#include "lwip/err.h"

#define HTTPS_CLIENT_TEMP_SIGNAL_MBEDTLS_RX 0x80
#define HTTPS_CLIENT_TEMP_SIGNAL_REQUEST 0x01
#define HTTPS_CLIENT_TEMP_SIGNAL_REQUEST_DONE 0x02
#define HTTPS_CLIENT_TEMP_SIGNAL_CONN_CLOSE 0x04

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HTTPS_Transport_t HTTPS_Transport_t;
typedef struct altcp_tls_config altcp_tls_config;

HTTPS_Transport_t * HTTPS_Transport_Init(EventGroupHandle_t events);
err_t HTTPS_Transport_SSLConfigure(HTTPS_Transport_t * trans, const u8_t * ca, size_t ca_len);
altcp_tls_config * HTTPS_Transport_GetTlsConfig(HTTPS_Transport_t * trans);
bool HTTPS_Transport_IsInitialized(HTTPS_Transport_t * trans);
void HTTPS_Transport_Deinit(HTTPS_Transport_t * trans);

#ifdef __cplusplus
}
#endif

#endif // HTTPS_TRANSPORT_H
