/***************************************************************************
 * @file wifi_rcp.c
 * @brief Forwards non-Matter Wi-Fi traffic to a Linux host over SDIO.
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
#include "wifi_rcp.h"
#include "wifi_rcp_classifier.h"
#include "wifi_rcp_config.h"
#include "wifi_rcp_protocol.h"

#include "cmsis_os2.h"
#include "sl_si91x_constants.h"
#include "sl_si91x_host_interface.h"
#include "sl_si91x_peripheral_sdio_secondary.h"
#include "sl_si91x_sdio_secondary.h"
#include "sl_si91x_sdio_secondary_drv_config.h"
#include "sl_types.h"
#include "sl_wifi.h"
#include "sli_buffer_manager.h"
#include "sli_routing_utility_types.h"

#include "lwip/dns.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"

#include <stdbool.h>
#include <string.h>

#define EVT_HOST_WRITE (1u << 0)
#define EVT_DMA_DONE (1u << 1)
#define EVT_TX_QUEUED (1u << 2)

#define ROUND_UP(x, a) ((((x) + (a) -1u) / (a)) * (a))
#define TX_XFER_MAX_SIZE ROUND_UP(WIFI_RCP_HDR_SIZE + WIFI_RCP_MAX_FRAME_SIZE, TX_TRANSFER_BLOCK_SIZE)
#define SDIO_INT_UNMASK_ALL (SL_SDIO_WR_INT_UNMSK | SL_SDIO_RD_INT_UNMSK | SL_SDIO_CMD52_INT_UNMSK)

_Static_assert(WIFI_RCP_HDR_SIZE <= SL_SI91X_WIFI_PACKET_DESC_SIZE, "RCP header must fit in the NWP descriptor");
_Static_assert(TX_XFER_MAX_SIZE / TX_TRANSFER_BLOCK_SIZE <= UINT8_MAX, "SDIO block count exceeds driver limit");

sl_status_t __real_sli_si91x_wifi_data_packet_handler(void * rx_buffer, uint32_t packet_size,
                                                      sli_routing_utility_packet_status_handler_t packet_status_handler,
                                                      void * context);

static osEventFlagsId_t sEvents;
static osMessageQueueId_t sTxQueue;
static volatile bool sHostReady;
static wifi_rcp_link_status_t sLinkStatus;

static uint8_t sRxBuf[WIFI_RCP_RX_BUF_SIZE] __attribute__((aligned(4)));
static uint8_t sTxBuf[TX_XFER_MAX_SIZE] __attribute__((aligned(4)));

static volatile uint32_t sTxDropCount;
static volatile uint32_t sRxErrorCount;

static void sdio_event_cb(uint8_t events)
{
    if (events & HOST_INTR_RECEIVE_EVENT)
    {
        osEventFlagsSet(sEvents, EVT_HOST_WRITE);
    }
    if (events & HOST_INTR_CMD52_EVENT)
    {
        sl_si91x_sdio_secondary_set_interrupts(SDIO_INT_UNMASK_ALL);
    }
}

static void dma_done_cb(uint8_t dma_ch)
{
    (void) dma_ch;
    osEventFlagsSet(sEvents, EVT_DMA_DONE);
}

static void wait_dma_done(void)
{
    osEventFlagsWait(sEvents, EVT_DMA_DONE, osFlagsWaitAny, osWaitForever);
}

static void write_hdr(uint8_t * xfer, wifi_rcp_msg_type_t type, uint16_t len)
{
    wifi_rcp_hdr_t hdr = { .length = len, .type = (uint8_t) type, .reserved = 0 };
    memcpy(xfer, &hdr, sizeof(hdr));
}

// Blocks until the host has read the whole transfer.
static void send_to_host(uint8_t * xfer, uint32_t len)
{
    uint8_t blocks = (uint8_t) (ROUND_UP(len, TX_TRANSFER_BLOCK_SIZE) / TX_TRANSFER_BLOCK_SIZE);
    sl_si91x_sdio_secondary_send(blocks, xfer);
    sl_si91x_sdio_secondary_request_to_send();
    wait_dma_done();
}

static void send_frame_to_host(sl_wifi_buffer_t * buffer)
{
    sl_wifi_system_packet_t * packet = (sl_wifi_system_packet_t *) buffer->data;
    uint16_t len                     = packet->length;

    if (len <= WIFI_RCP_MAX_FRAME_SIZE)
    {
#if WIFI_RCP_ZERO_COPY_TX
        // The NWP descriptor in front of the frame is no longer needed and holds the RCP header.
        uint8_t * xfer = packet->data - WIFI_RCP_HDR_SIZE;
#else
        uint8_t * xfer = sTxBuf;
        memcpy(xfer + WIFI_RCP_HDR_SIZE, packet->data, len);
#endif
        write_hdr(xfer, WIFI_RCP_MSG_ETH_FRAME, len);
        send_to_host(xfer, WIFI_RCP_HDR_SIZE + len);
    }

    sli_buffer_manager_free_buffer(buffer);
}

static void update_link_status(bool force)
{
    // Unlocked read only gates on lwIP being up; the lock is needed past this point.
    if (!sHostReady || netif_default == NULL)
    {
        return;
    }

    wifi_rcp_link_status_t status = { 0 };

    LOCK_TCPIP_CORE();
    struct netif * nif = netif_default;
    status.link_up     = netif_is_up(nif) && netif_is_link_up(nif);
    memcpy(status.mac, nif->hwaddr, sizeof(status.mac));
#if LWIP_IPV4
    status.ipv4_addr    = ip4_addr_get_u32(netif_ip4_addr(nif));
    status.ipv4_netmask = ip4_addr_get_u32(netif_ip4_netmask(nif));
    status.ipv4_gateway = ip4_addr_get_u32(netif_ip4_gw(nif));
#if LWIP_DNS
    const ip_addr_t * dns = dns_getserver(0);
    if (IP_IS_V4(dns))
    {
        status.ipv4_dns = ip4_addr_get_u32(ip_2_ip4(dns));
    }
#endif
#endif
    UNLOCK_TCPIP_CORE();

    if (!force && memcmp(&status, &sLinkStatus, sizeof(status)) == 0)
    {
        return;
    }
    sLinkStatus = status;

    write_hdr(sTxBuf, WIFI_RCP_MSG_LINK_STATUS, sizeof(status));
    memcpy(sTxBuf + WIFI_RCP_HDR_SIZE, &status, sizeof(status));
    send_to_host(sTxBuf, WIFI_RCP_HDR_SIZE + sizeof(status));
}

static void forward_host_frame(const uint8_t * frame, uint16_t len)
{
    if (!sHostReady || !sLinkStatus.link_up)
    {
        return;
    }
    // Host must use the 917 MAC, otherwise the AP drops the frame.
    if (len < 12 || memcmp(frame + 6, sLinkStatus.mac, sizeof(sLinkStatus.mac)) != 0)
    {
        return;
    }
    if (!wifi_rcp_host_tx_allowed(frame, len))
    {
        return;
    }
    sl_wifi_send_raw_data_frame(SL_WIFI_CLIENT_INTERFACE, frame, len);
}

static void handle_host_write(void)
{
    uint32_t total = sl_si91x_sdio_secondary_get_block_cnt() * sl_si91x_sdio_secondary_get_block_len();
    if (total < WIFI_RCP_HDR_SIZE || total > sizeof(sRxBuf))
    {
        sRxErrorCount++;
        sl_si91x_sdio_secondary_set_interrupts(SDIO_INT_UNMASK_ALL);
        return;
    }

    sl_si91x_sdio_secondary_receive(sRxBuf);
    wait_dma_done();

    wifi_rcp_hdr_t hdr;
    memcpy(&hdr, sRxBuf, sizeof(hdr));
    if ((uint32_t) WIFI_RCP_HDR_SIZE + hdr.length > total)
    {
        sRxErrorCount++;
        return;
    }

    switch (hdr.type)
    {
    case WIFI_RCP_MSG_ETH_FRAME:
        forward_host_frame(sRxBuf + WIFI_RCP_HDR_SIZE, hdr.length);
        break;
    case WIFI_RCP_MSG_LINK_STATUS_REQ:
        sHostReady = true;
        update_link_status(true);
        break;
    default:
        sRxErrorCount++;
        break;
    }
}

static void drain_tx_queue(void)
{
    sl_wifi_buffer_t * buffer;
    while (osMessageQueueGet(sTxQueue, &buffer, NULL, 0) == osOK)
    {
        send_frame_to_host(buffer);
        if (osEventFlagsGet(sEvents) & EVT_HOST_WRITE)
        {
            osEventFlagsClear(sEvents, EVT_HOST_WRITE);
            handle_host_write();
        }
    }
}

static void wifi_rcp_task(void * arg)
{
    (void) arg;
    const uint32_t poll_ticks = (WIFI_RCP_LINK_POLL_MS * osKernelGetTickFreq()) / 1000u;
    for (;;)
    {
        uint32_t flags = osEventFlagsWait(sEvents, EVT_HOST_WRITE | EVT_TX_QUEUED, osFlagsWaitAny, poll_ticks);
        if (flags & osFlagsError)
        {
            flags = 0;
        }
        if (flags & EVT_HOST_WRITE)
        {
            handle_host_write();
        }
        drain_tx_queue();
        update_link_status(false);
    }
}

// Runs in the SiWx91x HAL thread for every frame coming from the NWP.
sl_status_t __wrap_sli_si91x_wifi_data_packet_handler(void * rx_buffer, uint32_t packet_size,
                                                      sli_routing_utility_packet_status_handler_t packet_status_handler,
                                                      void * context)
{
    sl_wifi_buffer_t * buffer        = (sl_wifi_buffer_t *) rx_buffer;
    sl_wifi_system_packet_t * packet = (sl_wifi_system_packet_t *) buffer->data;

    if (!sHostReady || packet->command != SLI_RECEIVE_RAW_DATA)
    {
        return __real_sli_si91x_wifi_data_packet_handler(rx_buffer, packet_size, packet_status_handler, context);
    }

    // Strip the firmware queue ID from the length field, as the original handler does.
    packet->desc[1] &= 0x0F;

    switch (wifi_rcp_classify_rx(packet->data, packet->length))
    {
    case WIFI_RCP_ROUTE_M4:
        return __real_sli_si91x_wifi_data_packet_handler(rx_buffer, packet_size, packet_status_handler, context);
    case WIFI_RCP_ROUTE_BOTH:
        // Copies the frame into an lwIP pbuf, the buffer stays owned by us.
        sl_si91x_host_process_data_frame(SL_WIFI_CLIENT_INTERFACE, buffer);
        break;
    case WIFI_RCP_ROUTE_HOST:
        break;
    }

    if (osMessageQueuePut(sTxQueue, &buffer, 0, 0) != osOK)
    {
        sTxDropCount++;
        sli_buffer_manager_free_buffer(buffer);
        return SL_STATUS_OK;
    }
    osEventFlagsSet(sEvents, EVT_TX_QUEUED);
    return SL_STATUS_OK;
}

void wifi_rcp_init(void)
{
    static const osThreadAttr_t attr = {
        .name       = "wifi_rcp",
        .stack_size = WIFI_RCP_TASK_STACK_SIZE,
        .priority   = osPriorityNormal,
    };

    sEvents  = osEventFlagsNew(NULL);
    sTxQueue = osMessageQueueNew(WIFI_RCP_TX_QUEUE_DEPTH, sizeof(sl_wifi_buffer_t *), NULL);
    if (sEvents == NULL || sTxQueue == NULL)
    {
        return;
    }

    if (sl_si91x_sdio_secondary_register_event_callback(sdio_event_cb,
                                                        SL_SDIO_WR_INT_EN | SL_SDIO_RD_INT_EN | SL_SDIO_CMD52_INT_EN) !=
        SL_STATUS_OK)
    {
        return;
    }
    if (sl_si91x_sdio_secondary_gpdma_register_event_callback(dma_done_cb) != SL_STATUS_OK)
    {
        return;
    }

    osThreadNew(wifi_rcp_task, NULL, &attr);
}
