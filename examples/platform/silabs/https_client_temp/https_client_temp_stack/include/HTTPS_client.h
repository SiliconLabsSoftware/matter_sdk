/**
 * @file
 * @brief Minimal HTTPS GET client using forked altcp TLS (HTTPS Client Temp).
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 */

#ifndef HTTPS_CLIENT_H
#define HTTPS_CLIENT_H

#include "HTTPS_transport.h"
#include "lwip/err.h"
#include "lwip/ip_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*https_client_temp_http_done_fn)(void * arg, err_t err, int http_status, u32_t rx_bytes);

err_t HTTPS_Client_Get(HTTPS_Transport_t * transport, const ip_addr_t * server_addr, u16_t port, const char * host_header,
                       const char * uri, https_client_temp_http_done_fn done_fn, void * done_arg);

void https_client_temp_transport_process_mbedtls_rx(void);

#ifdef __cplusplus
}
#endif

#endif // HTTPS_CLIENT_H
