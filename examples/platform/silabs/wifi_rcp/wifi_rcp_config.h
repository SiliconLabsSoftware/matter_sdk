/***************************************************************************
 * @file wifi_rcp_config.h
 * @brief Configuration of the Wi-Fi to SDIO forwarding path.
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

// Ports owned by the M4 (Matter / lwIP). Everything else is forwarded to the host.
#ifndef WIFI_RCP_MATTER_PORT
#define WIFI_RCP_MATTER_PORT 5540
#endif
#ifndef WIFI_RCP_MATTER_UDC_PORT
#define WIFI_RCP_MATTER_UDC_PORT 5550
#endif
#ifndef WIFI_RCP_DHCP_CLIENT_PORT
#define WIFI_RCP_DHCP_CLIENT_PORT 68
#endif
// mDNS is delivered to both sides.
#ifndef WIFI_RCP_MDNS_PORT
#define WIFI_RCP_MDNS_PORT 5353
#endif
// Must match lwIP UDP/TCP_LOCAL_PORT_RANGE_START; the host ephemeral range must not overlap.
#ifndef WIFI_RCP_M4_EPHEMERAL_PORT_START
#define WIFI_RCP_M4_EPHEMERAL_PORT_START 0xC000
#endif

#ifndef WIFI_RCP_MAX_FRAME_SIZE
#define WIFI_RCP_MAX_FRAME_SIZE 1514
#endif

// Max NWP RX buffers held while waiting for the host to read them.
#ifndef WIFI_RCP_TX_QUEUE_DEPTH
#define WIFI_RCP_TX_QUEUE_DEPTH 8
#endif

// 1: DMA frames to the host straight from NWP RX buffers.
// The DMA reads up to (SDIO block size - 1) bytes past the frame end to pad the last block.
#ifndef WIFI_RCP_ZERO_COPY_TX
#define WIFI_RCP_ZERO_COPY_TX 1
#endif

#ifndef WIFI_RCP_RX_BUF_SIZE
#define WIFI_RCP_RX_BUF_SIZE 2048
#endif

#ifndef WIFI_RCP_LINK_POLL_MS
#define WIFI_RCP_LINK_POLL_MS 1000
#endif

#ifndef WIFI_RCP_TASK_STACK_SIZE
#define WIFI_RCP_TASK_STACK_SIZE 2048
#endif
