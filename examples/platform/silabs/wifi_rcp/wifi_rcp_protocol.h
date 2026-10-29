/***************************************************************************
 * @file wifi_rcp_protocol.h
 * @brief Wire format exchanged between the SiWx917 and the Linux host over SDIO.
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

#include <stdint.h>

// Every SDIO transfer starts with a wifi_rcp_hdr_t and is padded to the SDIO block size.
// All multi-byte fields are little endian unless stated otherwise.
// The host must read pending device data (card interrupt) before issuing a write.

#define WIFI_RCP_HDR_SIZE 4u

typedef enum
{
    WIFI_RCP_MSG_ETH_FRAME       = 0x01, // Both directions, payload is an Ethernet II frame
    WIFI_RCP_MSG_LINK_STATUS     = 0x02, // Device -> host, payload is wifi_rcp_link_status_t
    WIFI_RCP_MSG_LINK_STATUS_REQ = 0x03, // Host -> device, no payload; enables frame forwarding
} wifi_rcp_msg_type_t;

typedef struct __attribute__((packed))
{
    uint16_t length; // Payload length, header excluded
    uint8_t type;    // wifi_rcp_msg_type_t
    uint8_t reserved;
} wifi_rcp_hdr_t;

typedef struct __attribute__((packed))
{
    uint8_t link_up;
    uint8_t mac[6]; // Host netdev must use this MAC
    uint8_t reserved;
    uint32_t ipv4_addr; // Network byte order
    uint32_t ipv4_netmask;
    uint32_t ipv4_gateway;
    uint32_t ipv4_dns;
} wifi_rcp_link_status_t;

_Static_assert(sizeof(wifi_rcp_hdr_t) == WIFI_RCP_HDR_SIZE, "Unexpected RCP header size");
