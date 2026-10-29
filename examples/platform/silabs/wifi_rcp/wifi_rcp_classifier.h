/***************************************************************************
 * @file wifi_rcp_classifier.h
 * @brief Splits Wi-Fi traffic between the M4 (Matter) and the SDIO host.
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    WIFI_RCP_ROUTE_M4,
    WIFI_RCP_ROUTE_HOST,
    WIFI_RCP_ROUTE_BOTH,
} wifi_rcp_route_t;

// Route for an Ethernet frame received from Wi-Fi.
wifi_rcp_route_t wifi_rcp_classify_rx(const uint8_t * frame, size_t len);

// False if a host frame would use a source port owned by the M4.
bool wifi_rcp_host_tx_allowed(const uint8_t * frame, size_t len);

#ifdef __cplusplus
}
#endif
