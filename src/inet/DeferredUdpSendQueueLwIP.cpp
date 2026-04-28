/*
 *
 *    Copyright (c) 2020-2026 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include <inet/DeferredUdpSendQueueLwIP.h>

#if INET_CONFIG_UDP_LWIP_QUEUE_UNTIL_NETIF_READY

#include <inet/EndPointStateLwIP.h>
#include <inet/IPAddress.h>
#include <inet/IPPacketInfo.h>
#include <inet/UDPEndPoint.h>
#include <inet/UDPEndPointImplLwIP.h>

#include <lib/support/CodeUtils.h>
#include <lib/support/FixedBuffer.h>
#include <lib/support/logging/CHIPLogging.h>

#include <lwip/err.h>
#include <lwip/ip.h>
#include <lwip/netif.h>

#include <platform/LockTracker.h>

#include <utility>

namespace chip {
namespace Inet {

namespace {

#ifndef NETIF_FOREACH
#define NETIF_FOREACH(netif) for ((netif) = netif_list; (netif) != nullptr; (netif) = (netif)->next)
#endif

constexpr size_t kDeferredQueueCapacity = INET_CONFIG_UDP_LWIP_DEFERRED_SEND_QUEUE_SIZE;
static_assert(kDeferredQueueCapacity > 0, "INET_CONFIG_UDP_LWIP_DEFERRED_SEND_QUEUE_SIZE must be > 0");

struct DeferredUdpSlot
{
    UDPEndPointHandle epHandle;
    IPPacketInfo pktInfo;
    System::PacketBufferHandle msg;
};

struct DeferredUdpRing
{
    chip::FixedBuffer<DeferredUdpSlot, kDeferredQueueCapacity> slots{};
    size_t head  = 0;
    size_t count = 0;

    bool Empty() const { return count == 0; }

    void PushBack(DeferredUdpSlot && slot)
    {
        if (count == kDeferredQueueCapacity)
        {
            ChipLogDetail(Inet, "Deferred UDP queue full: dropping head");
            slots[head] = DeferredUdpSlot{};
            head        = (head + 1) % kDeferredQueueCapacity;
            --count;
        }
        const size_t idx = (head + count) % kDeferredQueueCapacity;
        slots[idx]       = std::move(slot);
        ++count;
    }

    DeferredUdpSlot PopFront()
    {
        DeferredUdpSlot out = std::move(slots[head]);
        slots[head]         = DeferredUdpSlot{};
        head                = (head + 1) % kDeferredQueueCapacity;
        --count;
        return out;
    }
};

/** Swap ring state (head, count) and per-slot storage without copying whole FixedBuffer (avoids copy-only path). */
void SwapDeferredRings(DeferredUdpRing & a, DeferredUdpRing & b)
{
    using std::swap;
    swap(a.head, b.head);
    swap(a.count, b.count);
    for (size_t i = 0; i < kDeferredQueueCapacity; ++i)
    {
        swap(a.slots[i], b.slots[i]);
    }
}

DeferredUdpRing gDeferredRing;

bool IsNetifUsable(struct netif * netif)
{
    return netif != nullptr && netif_is_up(netif) && netif_is_link_up(netif);
}

bool NetifHasPreferredIpv6Address(struct netif * netif)
{
    VerifyOrReturnValue(netif != nullptr, false);
    for (u8_t idx = 0; idx < LWIP_IPV6_NUM_ADDRESSES; ++idx)
    {
        if (ip6_addr_ispreferred(netif_ip6_addr_state(netif, idx)))
        {
            return true;
        }
    }
    return false;
}

bool IsNetifReadyForOutboundUdp(struct netif * netif, const IPAddress & dest)
{
    VerifyOrReturnValue(IsNetifUsable(netif), false);
    if (dest.IsIPv6())
    {
        return NetifHasPreferredIpv6Address(netif);
    }
#if INET_CONFIG_ENABLE_IPV4 && LWIP_IPV4
    if (dest.IsIPv4())
    {
        return !ip4_addr_isany(netif_ip4_addr(netif));
    }
#endif
    return false;
}

bool IsOutboundNetifReadyForUdp(const IPPacketInfo & pktInfo)
{
    const InterfaceId & intfId = pktInfo.Interface;
    const IPAddress & dest     = pktInfo.DestAddress;

    if (intfId.IsPresent())
    {
        return IsNetifReadyForOutboundUdp(intfId.GetPlatformInterface(), dest);
    }

    if (IsNetifReadyForOutboundUdp(netif_default, dest))
    {
        return true;
    }

    struct netif * netif = nullptr;
    NETIF_FOREACH(netif)
    {
        if (IsNetifReadyForOutboundUdp(netif, dest))
        {
            return true;
        }
    }

    return false;
}

} // namespace

CHIP_ERROR DeferredUdpSendQueueLwIP::ProbeDefer(const IPPacketInfo & pktInfo, bool & outShouldDefer)
{
    outShouldDefer = false;
    err_t probeErr = EndPointStateLwIP::RunOnTCPIPRet([&]() -> err_t {
        outShouldDefer = !IsOutboundNetifReadyForUdp(pktInfo);
        return ERR_OK;
    });
    VerifyOrReturnError(probeErr == ERR_OK, chip::System::MapErrorLwIP(probeErr));
    return CHIP_NO_ERROR;
}

CHIP_ERROR DeferredUdpSendQueueLwIP::Enqueue(UDPEndPointImplLwIP * self, const IPPacketInfo * pktInfo,
                                             System::PacketBufferHandle && msg)
{
    DeferredUdpSlot slot;
    slot.epHandle = UDPEndPointHandle(static_cast<UDPEndPoint *>(self));
    slot.pktInfo  = *pktInfo;
    slot.msg      = std::move(msg);

    ChipLogDetail(Inet, "UDP send deferred (waiting for netif)");

    gDeferredRing.PushBack(std::move(slot));
    return CHIP_NO_ERROR;
}

void DeferredUdpSendQueueLwIP::PurgeForEndpoint(UDPEndPointImplLwIP * self)
{
    UDPEndPoint * asBase = static_cast<UDPEndPoint *>(self);
    const size_t n       = gDeferredRing.count;
    for (size_t i = 0; i < n; ++i)
    {
        DeferredUdpSlot slot = gDeferredRing.PopFront();
        if (!slot.epHandle.IsNull() && slot.epHandle == *asBase)
        {
            continue;
        }
        gDeferredRing.PushBack(std::move(slot));
    }
}

void DeferredUdpSendQueueLwIP::Flush()
{
    assertChipStackLockedByCurrentThread();

    DeferredUdpRing pending{};
    SwapDeferredRings(gDeferredRing, pending);

    while (!pending.Empty())
    {
        DeferredUdpSlot slot = pending.PopFront();

        bool ready     = false;
        err_t probeErr = EndPointStateLwIP::RunOnTCPIPRet([&]() -> err_t {
            ready = IsOutboundNetifReadyForUdp(slot.pktInfo);
            return ERR_OK;
        });
        if (probeErr != ERR_OK)
        {
            ChipLogError(Inet, "DeferredUdpSendQueueLwIP::Flush: RunOnTCPIPRet failed: %d", static_cast<int>(probeErr));
            gDeferredRing.PushBack(std::move(slot));
            while (!pending.Empty())
            {
                gDeferredRing.PushBack(pending.PopFront());
            }
            return;
        }

        if (!ready)
        {
            gDeferredRing.PushBack(std::move(slot));
            continue;
        }

        if (!slot.epHandle.IsNull())
        {
            auto * impl = static_cast<UDPEndPointImplLwIP *>(slot.epHandle.operator->());
            (void) impl->FlushOneDeferred(&slot.pktInfo, std::move(slot.msg));
        }
    }
}

} // namespace Inet
} // namespace chip

#endif // INET_CONFIG_UDP_LWIP_QUEUE_UNTIL_NETIF_READY
