/***************************************************************************
 * @file wifi_rcp_classifier.c
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
#include "wifi_rcp_classifier.h"
#include "wifi_rcp_config.h"

#define ETH_HDR_LEN 14u
#define IPV4_MIN_HDR_LEN 20u
#define IPV6_HDR_LEN 40u
#define ARP_MIN_LEN 8u

#define ETHTYPE_IPV4 0x0800u
#define ETHTYPE_ARP 0x0806u
#define ETHTYPE_IPV6 0x86DDu

#define ARP_OP_REQUEST 1u

#define IP_PROTO_ICMP 1u
#define IP_PROTO_TCP 6u
#define IP_PROTO_UDP 17u
#define IP_PROTO_ICMPV6 58u

#define ICMP_ECHO_REQUEST 8u
#define ICMPV6_ECHO_REQUEST 128u
#define ICMPV6_ROUTER_SOLICITATION 133u
#define ICMPV6_NEIGHBOR_SOLICITATION 135u

#define IPV4_FRAG_OFFSET_MASK 0x1FFFu

typedef struct
{
    uint8_t proto;
    const uint8_t * l4;
    size_t l4_len;
    bool non_first_fragment;
} l4_info_t;

static uint16_t rd16(const uint8_t * p)
{
    return (uint16_t) ((p[0] << 8) | p[1]);
}

static bool is_m4_port(uint8_t proto, uint16_t port)
{
    if (port >= WIFI_RCP_M4_EPHEMERAL_PORT_START)
    {
        return true;
    }
    if (proto == IP_PROTO_TCP)
    {
        return port == WIFI_RCP_MATTER_PORT;
    }
    return port == WIFI_RCP_MATTER_PORT || port == WIFI_RCP_MATTER_UDC_PORT || port == WIFI_RCP_DHCP_CLIENT_PORT;
}

// Returns false if the frame is not IPv4/IPv6 or is malformed.
static bool parse_ip(const uint8_t * frame, size_t len, l4_info_t * info)
{
    const uint8_t * l3 = frame + ETH_HDR_LEN;
    size_t l3_len      = len - ETH_HDR_LEN;

    switch (rd16(frame + 12))
    {
    case ETHTYPE_IPV4: {
        if (l3_len < IPV4_MIN_HDR_LEN)
        {
            return false;
        }
        size_t ihl = (size_t) (l3[0] & 0x0F) * 4u;
        if (ihl < IPV4_MIN_HDR_LEN || ihl > l3_len)
        {
            return false;
        }
        info->proto              = l3[9];
        info->l4                 = l3 + ihl;
        info->l4_len             = l3_len - ihl;
        info->non_first_fragment = (rd16(l3 + 6) & IPV4_FRAG_OFFSET_MASK) != 0;
        return true;
    }
    case ETHTYPE_IPV6:
        if (l3_len < IPV6_HDR_LEN)
        {
            return false;
        }
        // Extension headers are not walked; they fall in the default (both) route.
        info->proto              = l3[6];
        info->l4                 = l3 + IPV6_HDR_LEN;
        info->l4_len             = l3_len - IPV6_HDR_LEN;
        info->non_first_fragment = false;
        return true;
    default:
        return false;
    }
}

wifi_rcp_route_t wifi_rcp_classify_rx(const uint8_t * frame, size_t len)
{
    if (len < ETH_HDR_LEN)
    {
        return WIFI_RCP_ROUTE_M4;
    }

    if (rd16(frame + 12) == ETHTYPE_ARP)
    {
        // Only the M4 answers requests for the shared IP; replies feed both ARP caches.
        if (len < ETH_HDR_LEN + ARP_MIN_LEN)
        {
            return WIFI_RCP_ROUTE_M4;
        }
        return rd16(frame + ETH_HDR_LEN + 6) == ARP_OP_REQUEST ? WIFI_RCP_ROUTE_M4 : WIFI_RCP_ROUTE_BOTH;
    }

    l4_info_t info;
    if (!parse_ip(frame, len, &info))
    {
        return WIFI_RCP_ROUTE_M4;
    }
    if (info.non_first_fragment)
    {
        return WIFI_RCP_ROUTE_BOTH;
    }

    switch (info.proto)
    {
    case IP_PROTO_UDP:
    case IP_PROTO_TCP: {
        if (info.l4_len < 4)
        {
            return WIFI_RCP_ROUTE_M4;
        }
        uint16_t dst_port = rd16(info.l4 + 2);
        if (info.proto == IP_PROTO_UDP && dst_port == WIFI_RCP_MDNS_PORT)
        {
            return WIFI_RCP_ROUTE_BOTH;
        }
        return is_m4_port(info.proto, dst_port) ? WIFI_RCP_ROUTE_M4 : WIFI_RCP_ROUTE_HOST;
    }
    case IP_PROTO_ICMP:
        if (info.l4_len < 1)
        {
            return WIFI_RCP_ROUTE_M4;
        }
        return info.l4[0] == ICMP_ECHO_REQUEST ? WIFI_RCP_ROUTE_M4 : WIFI_RCP_ROUTE_BOTH;
    case IP_PROTO_ICMPV6:
        if (info.l4_len < 1)
        {
            return WIFI_RCP_ROUTE_M4;
        }
        switch (info.l4[0])
        {
        case ICMPV6_ECHO_REQUEST:
        case ICMPV6_ROUTER_SOLICITATION:
        case ICMPV6_NEIGHBOR_SOLICITATION:
            return WIFI_RCP_ROUTE_M4;
        default:
            return WIFI_RCP_ROUTE_BOTH;
        }
    default:
        return WIFI_RCP_ROUTE_BOTH;
    }
}

bool wifi_rcp_host_tx_allowed(const uint8_t * frame, size_t len)
{
    if (len < ETH_HDR_LEN || len > WIFI_RCP_MAX_FRAME_SIZE)
    {
        return false;
    }

    l4_info_t info;
    if (!parse_ip(frame, len, &info) || info.non_first_fragment)
    {
        return true;
    }
    if ((info.proto != IP_PROTO_UDP && info.proto != IP_PROTO_TCP) || info.l4_len < 2)
    {
        return true;
    }

    uint16_t src_port = rd16(info.l4);
    if (info.proto == IP_PROTO_UDP && src_port == WIFI_RCP_MDNS_PORT)
    {
        return true;
    }
    return !is_m4_port(info.proto, src_port);
}
