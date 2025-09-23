/*
 *
 *    Copyright (c) 2025 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/silabs/wifi/dns-sd/LwIPApplicationCallbacks.h>

extern "C" {
#include "lwip/netif.h"
#include "lwip/prot/icmp6.h"
#include "lwip/prot/ip6.h"
#include "lwip/raw.h"
}

namespace {

struct raw_pcb * sRawPCB = nullptr;

// Minimal ICMPv6 RA receive handler
uint8_t Icmp6RawRecvHandler(void * arg, struct raw_pcb * pcb, struct pbuf * p, const ip_addr_t * addr)
{
    // Check for minimum length: IPv6 header + ICMPv6 header
    if (p == nullptr || p->tot_len < (sizeof(struct ip6_hdr) + sizeof(struct icmp6_hdr)))
        return 0;

    // Offset to ICMPv6 header (after IPv6 header)
    uint8_t * payload        = (uint8_t *) p->payload;
    struct icmp6_hdr * icmp6 = (struct icmp6_hdr *) (payload + sizeof(struct ip6_hdr));

    if (icmp6->type == ICMP6_TYPE_RA)
    {
        ChipLogProgress(DeviceLayer, "[SimpleRAHook] Router Advertisement received!");

        // The RA header follows the ICMPv6 header
        struct ra_header * ra = (struct ra_header *) (icmp6);

        // Router Lifetime is in network byte order, convert to host order
        uint16_t router_lifetime = lwip_ntohs(ra->router_lifetime);

        ChipLogProgress(DeviceLayer, "[SimpleRAHook] Router Lifetime: %u seconds", router_lifetime);
    }

    return 0; // Return 0 to allow further processing by the stack
}

} // namespace

namespace chip {
namespace Silabs {
namespace LwIP {

CHIP_ERROR InitRouterAdvertisementHook(void)
{
    VerifyOrReturnError(!sRawPCB, CHIP_ERROR_INCORRECT_STATE);

    sRawPCB = raw_new_ip_type(IPADDR_TYPE_V6, IP6_NEXTH_ICMP6);
    VerifyOrReturnError(sRawPCB != nullptr, CHIP_ERROR_NO_MEMORY,
                        ChipLogError(DeviceLayer, "Failed to allocated RA control block."));

    // Register receive handler
    raw_recv(sRawPCB, Icmp6RawRecvHandler, nullptr);

    // Optionally, bind to a specific netif or join multicast group if needed
    // raw_bind_netif(sRawPCB, ...);

    ChipLogProgress(DeviceLayer, "[SimpleRAHook] RA hook initialized\n");
    return CHIP_NO_ERROR;
}

} // namespace LwIP
} // namespace Silabs
} // namespace chip
