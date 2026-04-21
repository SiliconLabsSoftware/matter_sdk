/*
 *
 *    Copyright (c) 2020-2025 Project CHIP Authors
 *    Copyright (c) 2018 Google LLC.
 *    Copyright (c) 2013-2018 Nest Labs, Inc.
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

/**
 * This file implements Inet::UDPEndPoint using LwIP.
 */

#include <inet/UDPEndPointImplLwIP.h>

#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>

#if INET_CONFIG_ENABLE_IPV4
#include <lwip/igmp.h>
#endif // INET_CONFIG_ENABLE_IPV4

#include <lwip/err.h>
#include <lwip/init.h>
#include <lwip/ip.h>
#include <lwip/mld6.h>
#include <lwip/netif.h>
#include <lwip/raw.h>
#include <lwip/udp.h>

static_assert(LWIP_VERSION_MAJOR > 1, "CHIP requires LwIP 2.0 or later");

#if !defined(RAW_FLAGS_MULTICAST_LOOP) || !defined(UDP_FLAGS_MULTICAST_LOOP) || !defined(raw_clear_flags) ||                       \
    !defined(raw_set_flags) || !defined(udp_clear_flags) || !defined(udp_set_flags)
#define HAVE_LWIP_MULTICAST_LOOP 0
#else
#define HAVE_LWIP_MULTICAST_LOOP 1
#endif // !defined(RAW_FLAGS_MULTICAST_LOOP) || !defined(UDP_FLAGS_MULTICAST_LOOP) || !defined(raw_clear_flags) ||
       // !defined(raw_set_flags) || !defined(udp_clear_flags) || !defined(udp_set_flags)

// unusual define check for LWIP_IPV6_ND is because espressif fork
// of LWIP does not define the _ND constant.
#if LWIP_IPV6_MLD && (!defined(LWIP_IPV6_ND) || LWIP_IPV6_ND) && LWIP_IPV6
#define HAVE_IPV6_MULTICAST
#else
// Within Project CHIP multicast support is highly desirable: used for mDNS
// as well as group communication.
#undef HAVE_IPV6_MULTICAST
#endif

namespace chip {
namespace Inet {

#if INET_CONFIG_UDP_LWIP_QUEUE_UNTIL_NETIF_READY

namespace {

struct DeferredUdpSlot
{
    bool inUse = false;
    UDPEndPointHandle ep;
    IPPacketInfo pktInfo;
    System::PacketBufferHandle msg;
};

DeferredUdpSlot gDeferredUdpSlots[INET_CONFIG_UDP_LWIP_DEFERRED_SEND_QUEUE_SIZE];
size_t gDeferredUdpCount = 0;

bool IsNetifUsable(struct netif * netif)
{
    return netif != nullptr && netif_is_up(netif) && netif_is_link_up(netif);
}

#if LWIP_IPV6
bool NetifHasValidIpv6Address(struct netif * netif)
{
    if (netif == nullptr)
    {
        return false;
    }
    for (u8_t idx = 0; idx < LWIP_IPV6_NUM_ADDRESSES; ++idx)
    {
        if (ip6_addr_isvalid(netif_ip6_addr_state(netif, idx)))
        {
            return true;
        }
    }
    return false;
}
#endif // LWIP_IPV6

bool IsNetifReadyForOutboundUdp(struct netif * netif, const IPAddress & dest)
{
    if (!IsNetifUsable(netif))
    {
        return false;
    }
#if LWIP_IPV6
    if (dest.IsIPv6())
    {
        return NetifHasValidIpv6Address(netif);
    }
#endif // LWIP_IPV6
#if INET_CONFIG_ENABLE_IPV4 && LWIP_IPV4
    if (dest.IsIPv4())
    {
        return !ip4_addr_isany(netif_ip4_addr(netif));
    }
#endif // INET_CONFIG_ENABLE_IPV4 && LWIP_IPV4
    return true;
}

bool IsOutboundNetifReadyForUdp(const IPPacketInfo & pktInfo)
{
    const InterfaceId & intfId = pktInfo.Interface;
    const IPAddress & dest    = pktInfo.DestAddress;

    if (intfId.IsPresent())
    {
        return IsNetifReadyForOutboundUdp(intfId.GetPlatformInterface(), dest);
    }

    if (IsNetifReadyForOutboundUdp(netif_default, dest))
    {
        return true;
    }

#if defined(NETIF_FOREACH)
    struct netif * netif = nullptr;
    NETIF_FOREACH(netif)
    {
        if (IsNetifReadyForOutboundUdp(netif, dest))
        {
            return true;
        }
    }
#else
    for (struct netif * netif = netif_list; netif != nullptr; netif = netif->next)
    {
        if (IsNetifReadyForOutboundUdp(netif, dest))
        {
            return true;
        }
    }
#endif

    return false;
}

CHIP_ERROR EnqueueDeferredUdpSend(UDPEndPointImplLwIP * self, const IPPacketInfo * pktInfo, System::PacketBufferHandle && msg)
{
    if (gDeferredUdpCount >= INET_CONFIG_UDP_LWIP_DEFERRED_SEND_QUEUE_SIZE)
    {
        size_t head = INET_CONFIG_UDP_LWIP_DEFERRED_SEND_QUEUE_SIZE;
        for (size_t i = 0; i < INET_CONFIG_UDP_LWIP_DEFERRED_SEND_QUEUE_SIZE; ++i)
        {
            if (gDeferredUdpSlots[i].inUse)
            {
                head = i;
                break;
            }
        }
        if (head >= INET_CONFIG_UDP_LWIP_DEFERRED_SEND_QUEUE_SIZE)
        {
            ChipLogError(Inet, "Deferred UDP send queue inconsistent (count %u)",
                         static_cast<unsigned>(gDeferredUdpCount));
            return CHIP_ERROR_NO_MEMORY;
        }

        char dropDest[IPAddress::kMaxStringLength];
        gDeferredUdpSlots[head].pktInfo.DestAddress.ToString(dropDest);
        ChipLogProgress(Inet, "Deferred UDP queue full: dropping head dest %s port %u len %u", dropDest,
                        gDeferredUdpSlots[head].pktInfo.DestPort,
                        static_cast<unsigned>(gDeferredUdpSlots[head].msg->TotalLength()));

        gDeferredUdpSlots[head].inUse = false;
        gDeferredUdpSlots[head].ep    = UDPEndPointHandle();
        gDeferredUdpSlots[head].msg   = nullptr;
        --gDeferredUdpCount;
    }

    for (size_t i = 0; i < INET_CONFIG_UDP_LWIP_DEFERRED_SEND_QUEUE_SIZE; ++i)
    {
        if (!gDeferredUdpSlots[i].inUse)
        {
            gDeferredUdpSlots[i].inUse   = true;
            gDeferredUdpSlots[i].ep      = UDPEndPointHandle(static_cast<UDPEndPoint *>(self));
            gDeferredUdpSlots[i].pktInfo = *pktInfo;
            gDeferredUdpSlots[i].msg     = std::move(msg);
            ++gDeferredUdpCount;
            {
                char destStr[IPAddress::kMaxStringLength];
                pktInfo->DestAddress.ToString(destStr);
                ChipLogProgress(Inet, "UDP send deferred (waiting for netif): dest %s port %u len %u", destStr, pktInfo->DestPort,
                              static_cast<unsigned>(gDeferredUdpSlots[i].msg->TotalLength()));
            }
            return CHIP_NO_ERROR;
        }
    }

    return CHIP_ERROR_NO_MEMORY;
}

void PurgeDeferredUdpSendsForEndpoint(UDPEndPointImplLwIP * self)
{
    UDPEndPoint * asBase = static_cast<UDPEndPoint *>(self);
    for (size_t i = 0; i < INET_CONFIG_UDP_LWIP_DEFERRED_SEND_QUEUE_SIZE; ++i)
    {
        if (!gDeferredUdpSlots[i].inUse)
        {
            continue;
        }
        if (!gDeferredUdpSlots[i].ep.IsNull() && gDeferredUdpSlots[i].ep == *asBase)
        {
            gDeferredUdpSlots[i].inUse = false;
            gDeferredUdpSlots[i].ep    = UDPEndPointHandle();
            gDeferredUdpSlots[i].msg   = nullptr;
            --gDeferredUdpCount;
        }
    }
}

} // namespace

#endif // INET_CONFIG_UDP_LWIP_QUEUE_UNTIL_NETIF_READY

EndpointQueueFilter * UDPEndPointImplLwIP::sQueueFilter = nullptr;

CHIP_ERROR UDPEndPointImplLwIP::BindImpl(IPAddressType addressType, const IPAddress & address, uint16_t port,
                                         InterfaceId interfaceId)
{
    // Make sure we have the appropriate type of PCB.
    CHIP_ERROR res = GetPCB(addressType);

    // Bind the PCB to the specified address/port.
    ip_addr_t ipAddr;
    if (res == CHIP_NO_ERROR)
    {
        res = address.ToLwIPAddr(addressType, ipAddr);
    }

    if (res == CHIP_NO_ERROR)
    {
        res = chip::System::MapErrorLwIP(RunOnTCPIPRet([this, &ipAddr, port]() { return udp_bind(mUDP, &ipAddr, port); }));
    }

    if (res == CHIP_NO_ERROR)
    {
        res = LwIPBindInterface(mUDP, interfaceId);
    }

    return res;
}

CHIP_ERROR UDPEndPointImplLwIP::BindInterfaceImpl(IPAddressType addrType, InterfaceId intfId)
{
    // NOTE: this only supports LwIP interfaces whose number is no bigger than 9.

    // Make sure we have the appropriate type of PCB.
    CHIP_ERROR err = GetPCB(addrType);

    if (err == CHIP_NO_ERROR)
    {
        err = LwIPBindInterface(mUDP, intfId);
    }
    return err;
}

CHIP_ERROR UDPEndPointImplLwIP::LwIPBindInterface(struct udp_pcb * aUDP, InterfaceId intfId)
{
    struct netif * netifp = nullptr;
    if (intfId.IsPresent())
    {
        netifp = UDPEndPointImplLwIP::FindNetifFromInterfaceId(intfId);
        if (netifp == nullptr)
        {
            return INET_ERROR_UNKNOWN_INTERFACE;
        }
    }

    RunOnTCPIP([aUDP, netifp]() { udp_bind_netif(aUDP, netifp); });
    return CHIP_NO_ERROR;
}

InterfaceId UDPEndPointImplLwIP::GetBoundInterface() const
{
    struct netif * netif;
    RunOnTCPIP([this, &netif]() { netif = netif_get_by_index(mUDP->netif_idx); });
    return InterfaceId(netif);
}

uint16_t UDPEndPointImplLwIP::GetBoundPort() const
{
    return mUDP->local_port;
}

CHIP_ERROR UDPEndPointImplLwIP::ListenImpl()
{
    RunOnTCPIP([this]() { udp_recv(mUDP, LwIPReceiveUDPMessage, this); });
    return CHIP_NO_ERROR;
}

CHIP_ERROR UDPEndPointImplLwIP::SendMsgImpl(const IPPacketInfo * pktInfo, System::PacketBufferHandle && msg)
{
    assertChipStackLockedByCurrentThread();

    const IPAddress & destAddr = pktInfo->DestAddress;

    if (!msg.HasSoleOwnership())
    {
        // when retaining a buffer, the caller expects the msg to be unmodified.
        // LwIP stack will normally prepend the packet headers as the packet traverses
        // the UDP/IP/netif layers, which normally modifies the packet. We need to clone
        // msg into a fresh object in this case, and queues that for transmission, leaving
        // the original msg available after return.
        msg = msg.CloneData();
        VerifyOrReturnError(!msg.IsNull(), CHIP_ERROR_NO_MEMORY);
    }

    CHIP_ERROR res = GetPCB(destAddr.Type());
    if (res != CHIP_NO_ERROR)
    {
        return res;
    }

#if INET_CONFIG_UDP_LWIP_QUEUE_UNTIL_NETIF_READY
    FlushDeferredSendQueue();

    bool defer = false;
    err_t deferProbeErr = RunOnTCPIPRet([&]() -> err_t {
        defer = !IsOutboundNetifReadyForUdp(*pktInfo);
        return ERR_OK;
    });
    if (deferProbeErr != ERR_OK)
    {
        return chip::System::MapErrorLwIP(deferProbeErr);
    }
    if (defer)
    {
        return EnqueueDeferredUdpSend(this, pktInfo, std::move(msg));
    }
#endif // INET_CONFIG_UDP_LWIP_QUEUE_UNTIL_NETIF_READY

    return PerformLwIPUdpSend(pktInfo, std::move(msg));
}

CHIP_ERROR UDPEndPointImplLwIP::PerformLwIPUdpSend(const IPPacketInfo * pktInfo, System::PacketBufferHandle && msg)
{
    assertChipStackLockedByCurrentThread();

    const IPAddress & destAddr = pktInfo->DestAddress;

    // Send the message to the specified address/port.
    // If an outbound interface has been specified, call a specific version of the UDP sendto()
    // function that accepts the target interface.
    // If a source address has been specified, temporarily override the local_ip of the PCB.
    // This results in LwIP using the given address being as the source address for the generated
    // packet, as if the PCB had been bound to that address.
    const IPAddress & srcAddr   = pktInfo->SrcAddress;
    const uint16_t & destPort    = pktInfo->DestPort;
    const InterfaceId & intfId = pktInfo->Interface;

    ip_addr_t lwipSrcAddr   = srcAddr.ToLwIPAddr();
    ip_addr_t lwipDestAddr  = destAddr.ToLwIPAddr();
    ip_addr_t boundAddr;

    ip_addr_copy(boundAddr, mUDP->local_ip);

    if (!ip_addr_isany(&lwipSrcAddr))
    {
        ip_addr_copy(mUDP->local_ip, lwipSrcAddr);
    }

    err_t lwipErr = RunOnTCPIPRet([this, &intfId, &msg, &lwipDestAddr, destPort]() {
        if (intfId.IsPresent())
        {
            return udp_sendto_if(mUDP, System::LwIPPacketBufferView::UnsafeGetLwIPpbuf(msg), &lwipDestAddr, destPort,
                                 intfId.GetPlatformInterface());
        }
        return udp_sendto(mUDP, System::LwIPPacketBufferView::UnsafeGetLwIPpbuf(msg), &lwipDestAddr, destPort);
    });

    ip_addr_copy(mUDP->local_ip, boundAddr);

    const unsigned payloadLen = static_cast<unsigned>(msg->TotalLength());
    char destStr[IPAddress::kMaxStringLength];
    destAddr.ToString(destStr);

#if INET_CONFIG_UDP_LWIP_QUEUE_UNTIL_NETIF_READY
    if (lwipErr == ERR_RTE)
    {
        ChipLogProgress(Inet, "UDP send deferred (no route yet, ERR_RTE): dest %s port %u len %u", destStr, destPort, payloadLen);
        return EnqueueDeferredUdpSend(this, pktInfo, std::move(msg));
    }
#endif // INET_CONFIG_UDP_LWIP_QUEUE_UNTIL_NETIF_READY

    if (lwipErr != ERR_OK)
    {
        ChipLogError(Inet, "UDP send failed: dest %s port %u len %u lwip err %d", destStr, destPort, payloadLen,
                     static_cast<int>(lwipErr));
        return chip::System::MapErrorLwIP(lwipErr);
    }

    ChipLogProgress(Inet, "UDP send: dest %s port %u len %u", destStr, destPort, payloadLen);
    return CHIP_NO_ERROR;
}

#if INET_CONFIG_UDP_LWIP_QUEUE_UNTIL_NETIF_READY
void UDPEndPointImplLwIP::FlushDeferredSendQueue()
{
    assertChipStackLockedByCurrentThread();

    bool madeProgress = true;
    while (madeProgress)
    {
        madeProgress = false;
        for (size_t i = 0; i < INET_CONFIG_UDP_LWIP_DEFERRED_SEND_QUEUE_SIZE; ++i)
        {
            if (!gDeferredUdpSlots[i].inUse)
            {
                continue;
            }

            bool ready = false;
            err_t deferProbeErr = RunOnTCPIPRet([&]() -> err_t {
                ready = IsOutboundNetifReadyForUdp(gDeferredUdpSlots[i].pktInfo);
                return ERR_OK;
            });
            if (deferProbeErr != ERR_OK)
            {
                ChipLogError(Inet, "FlushDeferredSendQueue: RunOnTCPIPRet failed: %d", static_cast<int>(deferProbeErr));
                return;
            }
            if (!ready)
            {
                continue;
            }

            UDPEndPointHandle ep = std::move(gDeferredUdpSlots[i].ep);
            IPPacketInfo pktCopy = gDeferredUdpSlots[i].pktInfo;
            System::PacketBufferHandle buf = std::move(gDeferredUdpSlots[i].msg);
            gDeferredUdpSlots[i].inUse = false;
            --gDeferredUdpCount;

            if (!ep.IsNull())
            {
                auto * impl = static_cast<UDPEndPointImplLwIP *>(ep.operator->());
                if (impl->mState != State::kClosed && impl->mUDP != nullptr)
                {
                    (void) impl->PerformLwIPUdpSend(&pktCopy, std::move(buf));
                }
            }

            madeProgress = true;
            break;
        }
    }
}
#else
void UDPEndPointImplLwIP::FlushDeferredSendQueue() {}
#endif // INET_CONFIG_UDP_LWIP_QUEUE_UNTIL_NETIF_READY

void UDPEndPointImplLwIP::CloseImpl()
{
    assertChipStackLockedByCurrentThread();

#if INET_CONFIG_UDP_LWIP_QUEUE_UNTIL_NETIF_READY
    PurgeDeferredUdpSendsForEndpoint(this);
#endif

    // Since UDP PCB is released synchronously here, but UDP endpoint itself might have to wait
    // for destruction asynchronously, there could be more allocated UDP endpoints than UDP PCBs.
    if (mUDP == nullptr)
    {
        return;
    }
    RunOnTCPIP([this]() { udp_remove(mUDP); });
    mUDP              = nullptr;
    mLwIPEndPointType = LwIPEndPointType::Unknown;

    // If there is a UDPEndPointImplLwIP::LwIPReceiveUDPMessage
    // event pending in the event queue (SystemLayer::ScheduleLambda), we
    // schedule a unref call to the end of the queue, to ensure that the
    // queued pointer to UDPEndPointImplLwIP is not dangling.
    if (mDelayReleaseCount != 0)
    {
        Ref();
        CHIP_ERROR err = GetSystemLayer().ScheduleLambda([this] { Unref(); });
        if (err != CHIP_NO_ERROR)
        {
            ChipLogError(Inet, "Unable to schedule lambda: %" CHIP_ERROR_FORMAT, err.Format());
            // There is nothing we can do here, accept the chance of racing
            Unref();
        }
    }
}

void UDPEndPointImplLwIP::HandleDataReceived(System::PacketBufferHandle && msg, IPPacketInfo * pktInfo)
{
    // Process packet filter if needed. May cause packet to get dropped before processing.
    bool dropPacket = false;
    if ((pktInfo != nullptr) && (sQueueFilter != nullptr))
    {
        auto outcome = sQueueFilter->FilterAfterDequeue(this, *pktInfo, msg);
        dropPacket   = (outcome == EndpointQueueFilter::FilterOutcome::kDropPacket);
    }

    // Process actual packet if allowed
    if ((mState == State::kListening) && (OnMessageReceived != nullptr) && !dropPacket)
    {
        if (pktInfo != nullptr)
        {
            const IPPacketInfo pktInfoCopy = *pktInfo; // copy the address info so that the app can free the
                                                       // PacketBuffer without affecting access to address info.
            OnMessageReceived(this, std::move(msg), &pktInfoCopy);
        }
        else
        {
            if (OnReceiveError != nullptr)
            {
                OnReceiveError(this, CHIP_ERROR_INBOUND_MESSAGE_TOO_BIG, nullptr);
            }
        }
    }
    Platform::Delete(pktInfo);
}

CHIP_ERROR UDPEndPointImplLwIP::GetPCB(IPAddressType addrType)
{
    assertChipStackLockedByCurrentThread();

    // If a PCB hasn't been allocated yet...
    if (mUDP == nullptr)
    {
        // Allocate a PCB of the appropriate type.
        if (addrType == IPAddressType::kIPv6)
        {
            RunOnTCPIP([this]() { mUDP = udp_new_ip_type(IPADDR_TYPE_V6); });
        }
#if INET_CONFIG_ENABLE_IPV4
        else if (addrType == IPAddressType::kIPv4)
        {
            RunOnTCPIP([this]() { mUDP = udp_new_ip_type(IPADDR_TYPE_V4); });
        }
#endif // INET_CONFIG_ENABLE_IPV4
        else
        {
            return INET_ERROR_WRONG_ADDRESS_TYPE;
        }

        // Fail if the system has run out of PCBs.
        if (mUDP == nullptr)
        {
            ChipLogError(Inet, "Unable to allocate UDP PCB");
            return CHIP_ERROR_NO_MEMORY;
        }

        // Allow multiple bindings to the same port.
        ip_set_option(mUDP, SOF_REUSEADDR);
    }

    // Otherwise, verify that the existing PCB is the correct type...
    else
    {
        IPAddressType pcbAddrType;

        // Get the address type of the existing PCB.
        switch (static_cast<lwip_ip_addr_type>(IP_GET_TYPE(&mUDP->local_ip)))
        {
        case IPADDR_TYPE_V6:
            pcbAddrType = IPAddressType::kIPv6;
            break;
#if INET_CONFIG_ENABLE_IPV4
        case IPADDR_TYPE_V4:
            pcbAddrType = IPAddressType::kIPv4;
            break;
#endif // INET_CONFIG_ENABLE_IPV4
        default:
            return INET_ERROR_WRONG_ADDRESS_TYPE;
        }

        // Fail if the existing PCB is not the correct type.
        VerifyOrReturnError(addrType == pcbAddrType, INET_ERROR_WRONG_ADDRESS_TYPE);
    }

    return CHIP_NO_ERROR;
}

void UDPEndPointImplLwIP::LwIPReceiveUDPMessage(void * arg, struct udp_pcb * pcb, struct pbuf * p, const ip_addr_t * addr,
                                                u16_t port)
{
    UDPEndPointImplLwIP * ep = static_cast<UDPEndPointImplLwIP *>(arg);
    if (ep->mState == State::kClosed)
    {
        return;
    }

    auto pktInfo = Platform::MakeUnique<IPPacketInfo>();
    if (pktInfo.get() == nullptr)
    {
        ChipLogError(Inet, "Cannot allocate packet info");
        return;
    }

    System::PacketBufferHandle buf = System::PacketBufferHandle::Adopt(p);
    if (buf->HasChainedBuffer())
    {
        buf->CompactHead();
    }
    if (buf->HasChainedBuffer())
    {
        // Have to allocate a new big-enough buffer and copy.
        size_t messageSize              = buf->TotalLength();
        System::PacketBufferHandle copy = System::PacketBufferHandle::New(messageSize, 0);
        if (copy.IsNull() || buf->Read(copy->Start(), messageSize) != CHIP_NO_ERROR)
        {
            ChipLogError(Inet, "No memory to flatten incoming packet buffer chain of size %u", buf->TotalLength());
            return;
        }
        buf = std::move(copy);
    }

    pktInfo->SrcAddress  = IPAddress(*addr);
    pktInfo->DestAddress = IPAddress(*ip_current_dest_addr());
    pktInfo->Interface   = InterfaceId(ip_current_netif());
    pktInfo->SrcPort     = port;
    pktInfo->DestPort    = pcb->local_port;

    auto filterOutcome = EndpointQueueFilter::FilterOutcome::kAllowPacket;
    if (sQueueFilter != nullptr)
    {
        filterOutcome = sQueueFilter->FilterBeforeEnqueue(ep, *(pktInfo.get()), buf);
    }

    if (filterOutcome != EndpointQueueFilter::FilterOutcome::kAllowPacket)
    {
        // Logging, if any, should be at the choice of the filter impl at time of filtering.
        return;
    }

    // Increase mDelayReleaseCount to delay release of this UDP EndPoint while the HandleDataReceived call is
    // pending on it.
    ep->mDelayReleaseCount++;

    CHIP_ERROR err = ep->GetSystemLayer().ScheduleLambda(
        [ep, p = System::LwIPPacketBufferView::UnsafeGetLwIPpbuf(buf), pktInfo = pktInfo.get()] {
            ep->mDelayReleaseCount--;

            auto handle = System::PacketBufferHandle::Adopt(p);
            ep->HandleDataReceived(std::move(handle), pktInfo);
        });

    if (err == CHIP_NO_ERROR)
    {
        // If ScheduleLambda() succeeded, it has ownership of the buffer, so we need to release it (without freeing it).
        static_cast<void>(std::move(buf).UnsafeRelease());
        // Similarly, ScheduleLambda now has ownership of pktInfo.
        pktInfo.release();
    }
    else
    {
        // On failure to enqueue the processing, we have to tell the filter that
        // the packet is basically dequeued, if it tries to keep track of the lifecycle.
        if (sQueueFilter != nullptr)
        {
            (void) sQueueFilter->FilterAfterDequeue(ep, *(pktInfo.get()), buf);
            ChipLogError(Inet, "Dequeue ERROR err = %" CHIP_ERROR_FORMAT, err.Format());
        }

        ep->mDelayReleaseCount--;
    }
}

CHIP_ERROR UDPEndPointImplLwIP::SetMulticastLoopback(IPVersion aIPVersion, bool aLoopback)
{
#if HAVE_LWIP_MULTICAST_LOOP
    if (mLwIPEndPointType == LwIPEndPointType::UDP)
    {
        if (aLoopback)
        {
            udp_set_flags(mUDP, UDP_FLAGS_MULTICAST_LOOP);
        }
        else
        {
            udp_clear_flags(mUDP, UDP_FLAGS_MULTICAST_LOOP);
        }
        return CHIP_NO_ERROR;
    }
#endif // HAVE_LWIP_MULTICAST_LOOP
    return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
}

#if INET_CONFIG_ENABLE_IPV4
CHIP_ERROR UDPEndPointImplLwIP::IPv4JoinLeaveMulticastGroupImpl(InterfaceId aInterfaceId, const IPAddress & aAddress, bool join)
{
#if LWIP_IPV4 && LWIP_IGMP
    const ip4_addr_t lIPv4Address = aAddress.ToIPv4();
    struct netif * lNetif         = nullptr;
    if (aInterfaceId.IsPresent())
    {
        lNetif = FindNetifFromInterfaceId(aInterfaceId);
        VerifyOrReturnError(lNetif != nullptr, INET_ERROR_UNKNOWN_INTERFACE);
    }

    err_t lStatus = RunOnTCPIPRet([lNetif, &lIPv4Address, join]() {
        if (lNetif != nullptr)
        {
            return join ? igmp_joingroup_netif(lNetif, &lIPv4Address) //
                        : igmp_leavegroup_netif(lNetif, &lIPv4Address);
        }
        return join ? igmp_joingroup(IP4_ADDR_ANY4, &lIPv4Address) //
                    : igmp_leavegroup(IP4_ADDR_ANY4, &lIPv4Address);
    });

    if (lStatus == ERR_MEM)
    {
        return CHIP_ERROR_NO_MEMORY;
    }
    return chip::System::MapErrorLwIP(lStatus);
#else  // LWIP_IPV4 && LWIP_IGMP
    return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
#endif // LWIP_IPV4 && LWIP_IGMP
}
#endif // INET_CONFIG_ENABLE_IPV4

CHIP_ERROR UDPEndPointImplLwIP::IPv6JoinLeaveMulticastGroupImpl(InterfaceId aInterfaceId, const IPAddress & aAddress, bool join)
{
#ifdef HAVE_IPV6_MULTICAST
    const ip6_addr_t lIPv6Address = aAddress.ToIPv6();
    struct netif * lNetif         = nullptr;
    if (aInterfaceId.IsPresent())
    {
        lNetif = FindNetifFromInterfaceId(aInterfaceId);
        VerifyOrReturnError(lNetif != nullptr, INET_ERROR_UNKNOWN_INTERFACE);
    }

    err_t lStatus = RunOnTCPIPRet([lNetif, &lIPv6Address, join]() {
        if (lNetif != nullptr)
        {
            return join ? mld6_joingroup_netif(lNetif, &lIPv6Address) //
                        : mld6_leavegroup_netif(lNetif, &lIPv6Address);
        }
        return join ? mld6_joingroup(IP6_ADDR_ANY6, &lIPv6Address) //
                    : mld6_leavegroup(IP6_ADDR_ANY6, &lIPv6Address);
    });

    if (lStatus == ERR_MEM)
    {
        return CHIP_ERROR_NO_MEMORY;
    }

    return chip::System::MapErrorLwIP(lStatus);
#else  // HAVE_IPV6_MULTICAST
    return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
#endif // HAVE_IPV6_MULTICAST
}

struct netif * UDPEndPointImplLwIP::FindNetifFromInterfaceId(InterfaceId aInterfaceId)
{
    struct netif * lRetval = nullptr;

    RunOnTCPIP([aInterfaceId, &lRetval]() {
#if defined(NETIF_FOREACH)
        NETIF_FOREACH(lRetval)
        {
            if (lRetval == aInterfaceId.GetPlatformInterface())
            {
                break;
            }
        }
#else  // defined(NETIF_FOREACH)
        for (lRetval = netif_list; lRetval != nullptr && lRetval != aInterfaceId.GetPlatformInterface(); lRetval = lRetval->next)
            ;
#endif // defined(NETIF_FOREACH)
    });

    return (lRetval);
}

} // namespace Inet
} // namespace chip
