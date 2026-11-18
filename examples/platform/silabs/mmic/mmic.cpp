/*******************************************************************************
 * @file
 * @brief MMIC task using the matter_cpc transport.
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
#include "mmic.h"
#include "string.h"
#include "stdlib.h"

uint16_t crc16(const uint8_t * buffer, uint16_t size)
{
    uint16_t crc = 0xFFFF;
    while (size--)
    {
        crc ^= (uint16_t)(*buffer++) << 8;
        for (uint8_t i = 0; i < 8; i++)
        {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint8_t mmic_serialize_packet(uint8_t header, mmic_command_id_e id,
                              const void * payload, size_t payloadLen,
                              uint8_t ** encodedPacket, size_t * packetSize)
{
    if (id >= INVALID_COMMAND_ID || encodedPacket == NULL || packetSize == NULL)
    {
        return MMIC_ERROR_INVALID_ARG;
    }

    const size_t frameSize = (size_t) MMIC_PACKET_OVERHEAD + payloadLen;
    if (frameSize > UINT16_MAX)
    {
        return MMIC_ERROR_INVALID_ARG;
    }

    uint8_t * workbuffer = (uint8_t *) malloc(frameSize);
    if (workbuffer == NULL)
    {
        return MMIC_ERROR_NO_MEMORY;
    }

    workbuffer[0] = header;
    uint8_t err   = mmic_write_length(workbuffer, (uint16_t) frameSize);
    if (err != MMIC_ERROR_OK)
    {
        free(workbuffer);
        return err;
    }
    workbuffer[MMIC_OFFSET_OP] = (uint8_t) id;

    if (payloadLen > 0 && payload != NULL)
    {
        memcpy(workbuffer + MMIC_OFFSET_PAYLOAD, payload, payloadLen);
    }

    const uint16_t crc = crc16(workbuffer, (uint16_t) (frameSize - 2));
    memcpy(workbuffer + frameSize - 2, &crc, 2);

    *encodedPacket = workbuffer; // Warning: must be freed by the caller
    *packetSize    = frameSize;
    return MMIC_ERROR_OK;
}

uint8_t mmic_deserialize_packet(const uint8_t * buffer, size_t len, uint8_t expectedHeader,
                                mmic_command_id_e * outOpCode,
                                const uint8_t ** outPayload, uint16_t * outPayloadLen)
{
    if (buffer == NULL || len == 0)
    {
        return MMIC_ERROR_INVALID_ARG;
    }

    const uint16_t frameLen = mmic_read_length(buffer);
    if (buffer[0] != expectedHeader
        || buffer[MMIC_OFFSET_OP] >= INVALID_COMMAND_ID
        || frameLen < MMIC_PACKET_OVERHEAD
        || frameLen > len)
    {
        return MMIC_ERROR_INVALID_PACKET;
    }

    uint16_t crc = 0;
    memcpy(&crc, buffer + (frameLen - 2), 2);
    if (crc != crc16(buffer, (uint16_t) (frameLen - 2)))
    {
        return MMIC_ERROR_INVALID_PACKET;
    }

    if (outOpCode != NULL)
    {
        *outOpCode = (mmic_command_id_e) buffer[MMIC_OFFSET_OP];
    }
    if (outPayload != NULL)
    {
        *outPayload = buffer + MMIC_OFFSET_PAYLOAD;
    }
    if (outPayloadLen != NULL)
    {
        *outPayloadLen = (uint16_t) (frameLen - MMIC_PACKET_OVERHEAD);
    }
    return MMIC_ERROR_OK;
}

#if HOST_SIDE
#include <stdio.h>
#include "mmic_cluster_names.h"

static const commandsData_t commands[] =
{
    #define X(a,b,c, d) {.argsCnt=c, .argsSize= (c * sizeof(d))},
    COMMAND_LIST
    #undef X
};

const char commandsString[][255] = {
    #define X(a,b,c, d) #a,
    COMMAND_LIST
    #undef X
};


uint8_t encodeCommand(mmic_command_id_e id, void * parameter, uint16_t size, uint8_t ** encodedPacket, size_t * packetSize)
{
    if (id >= INVALID_COMMAND_ID)
    {
        return MMIC_ERROR_INVALID_ARG;
    }

    // For fixed-args commands defined via COMMAND_LIST, argsCnt/argsSize drive the wire size.
    // For variable-length commands (establish_subscription, commission) the caller passes size.
    const bool isVariableLen = (id == establish_subscription) || (id == commission);

    if (!isVariableLen && (commands[id].argsCnt == 0 && parameter != NULL))
    {
        return MMIC_ERROR_INVALID_ARG;
    }
    if (!isVariableLen && (commands[id].argsCnt != 0 && parameter == NULL))
    {
        return MMIC_ERROR_INVALID_ARG;
    }
    if (isVariableLen && (parameter == NULL || size == 0))
    {
        return MMIC_ERROR_INVALID_ARG;
    }

    // Determine payload size for this frame.
    uint16_t payloadSize = 0;
    if (commands[id].argsCnt == 0)
    {
        payloadSize = 0;
    }
    else if (isVariableLen)
    {
        payloadSize = size;
    }
    else
    {
        payloadSize = (uint16_t) (commands[id].argsSize);
    }

    return mmic_serialize_packet(MMIC_HEADER_CMD, id,
                                 (payloadSize > 0) ? parameter : NULL, payloadSize,
                                 encodedPacket, packetSize);
}

uint8_t decodeAndPrintResponse(uint8_t * buffer, size_t len)
{
    mmic_command_id_e opcode = INVALID_COMMAND_ID;
    const uint8_t * payload  = NULL;
    uint16_t payloadLen      = 0;
    uint8_t err = mmic_deserialize_packet(buffer, len, MMIC_HEADER_ANS, &opcode, &payload, &payloadLen);
    if (err != MMIC_ERROR_OK)
    {
        return err;
    }

    switch (opcode)
    {
        case ping:
            printf("\r\n%s\r\n", (const char *) payload);
            break;
        case version:
            printf("\r\n%s\r\n", (const char *) payload);
            break;
        case matter_state:
            matterState_t state;
            memcpy(&state, payload, sizeof(matterState_t));
            printf("\r\nNumber of Fabrics: %d\r\nCommisionning Window Open:  %s\r\n",state.nbOfFabric, (state.commissioningWindowOpen)? "true" : "false" );
            printf("mDNS Operational Advertising: %s\r\n", state.mdnsOperationalAdvertising ? "true" : "false");
            if (state.nbOfFabric > 0)
            {
                printf("Fabric[%u]: nodeId=0x%016llx fabricId=0x%016llx\r\n",
                       (unsigned) state.fabricIndex,
                       (unsigned long long) state.nodeId,
                       (unsigned long long) state.fabricId);
                printf("  compressedFabricId: ");
                for (size_t i = 0; i < sizeof(state.compressedFabricId); ++i)
                {
                    printf("%02x", state.compressedFabricId[i]);
                }
                printf("\r\n  rootPublicKey: ");
                for (size_t i = 0; i < sizeof(state.rootPublicKey); ++i)
                {
                    printf("%02x", state.rootPublicKey[i]);
                }
                printf("\r\n");
            }
            if (state.commissioningWindowOpen)
            {
                printf("mDNS Commissionable Advertising (_matterc._udp):\r\n"
                       "  Instance Name: %s\r\n"
                       "  UDP Port: %u\r\n"
                       "  Discriminator: 0x%03x (%u)\r\n",
                       state.mdnsCommissionableInstanceName,
                       state.mdnsCommissionableUdpPort,
                       state.setupDiscriminator, state.setupDiscriminator);
            }
            printf("Thread interface up: %s\r\nThread attached: %s\r\n",
                   state.threadInterfaceUp ? "true" : "false",
                   state.threadAttached ? "true" : "false");
            printf("Thread mesh-local EID: "
                   "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x\r\n",
                   state.threadMeshLocalEid[0],  state.threadMeshLocalEid[1],
                   state.threadMeshLocalEid[2],  state.threadMeshLocalEid[3],
                   state.threadMeshLocalEid[4],  state.threadMeshLocalEid[5],
                   state.threadMeshLocalEid[6],  state.threadMeshLocalEid[7],
                   state.threadMeshLocalEid[8],  state.threadMeshLocalEid[9],
                   state.threadMeshLocalEid[10], state.threadMeshLocalEid[11],
                   state.threadMeshLocalEid[12], state.threadMeshLocalEid[13],
                   state.threadMeshLocalEid[14], state.threadMeshLocalEid[15]);
            printf("Thread Network Name: %s\r\nThread PAN ID: 0x%04x\r\nThread Channel: %u\r\n",
                   state.threadNetworkName, state.threadPanId, state.threadChannel);
            printf("Thread Extended PAN ID: %02x%02x%02x%02x%02x%02x%02x%02x\r\n",
                   state.threadExtendedPanId[0], state.threadExtendedPanId[1],
                   state.threadExtendedPanId[2], state.threadExtendedPanId[3],
                   state.threadExtendedPanId[4], state.threadExtendedPanId[5],
                   state.threadExtendedPanId[6], state.threadExtendedPanId[7]);
            break;
        case openCommissioning:
        case commission:
        case decommission:
        case addWakeUp:
        case removeWakeUp:
            printf("\r\n%s : %d\r\n", payload[0] == 0 ? "Success" : "Failure", payload[0]);
            break;
        case establish_subscription:
        {
            if (payloadLen < sizeof(subscriptionEstablishResp_t)) {
                printf("\r\nestablish_subscription: truncated response\r\n");
                break;
            }
            subscriptionEstablishResp_t r;
            memcpy(&r, payload, sizeof(r));
            if (r.status == 0) {
                printf("\r\nestablish_subscription: Success (handle=%u)\r\n", (unsigned) r.handle);
            } else {
                printf("\r\nestablish_subscription: Failure (status=%u)\r\n", (unsigned) r.status);
            }
            break;
        }
        case subscription_info:
        {
            if (payloadLen < 1) { printf("\r\nsubscription_info: empty payload\r\n"); break; }
            uint8_t count = payload[0];
            const size_t expected = 1 + (size_t) count * sizeof(subscriptionEntry_t);
            if (payloadLen < expected) { printf("\r\nsubscription_info: truncated\r\n"); break; }
            printf("\r\nActive subscriptions (%u):\r\n", (unsigned) count);
            const uint8_t * p = payload + 1;
            for (uint8_t i = 0; i < count; ++i)
            {
                subscriptionEntry_t e;
                memcpy(&e, p + i * sizeof(subscriptionEntry_t), sizeof(e));
                printf("  [%u] fabric=%u node=0x%016llx endpoint=%u cluster=0x%08x attribute=0x%08x\r\n",
                       (unsigned) e.handle, (unsigned) e.fabricIndex,
                       (unsigned long long) e.nodeId, (unsigned) e.endpointId,
                       (unsigned) e.clusterId, (unsigned) e.attributeId);
            }
            break;
        }
        case wakeUpList:
        {
            if (payloadLen < 1) { printf("\r\nwakeUpList: empty payload\r\n"); break; }
            uint8_t count = payload[0];
            const size_t expected = 1 + (size_t) count * sizeof(wakeUpEntry_t);
            if (payloadLen < expected) { printf("\r\nwakeUpList: truncated\r\n"); break; }
            static const char * const kModeName[] = { "Boolean", "Bitmask", "Equal" };
            printf("\r\nActive wake-up triggers (%u):\r\n", (unsigned) count);
            const uint8_t * p = payload + 1;
            for (uint8_t i = 0; i < count; ++i)
            {
                wakeUpEntry_t e;
                memcpy(&e, p + i * sizeof(wakeUpEntry_t), sizeof(e));
                const char * modeStr = (e.mode < (sizeof(kModeName) / sizeof(kModeName[0]))) ? kModeName[e.mode] : "?";
                printf("  cluster=0x%08x attribute=0x%08x mode=%s(%u) operand=0x%016llx\r\n",
                       (unsigned) e.clusterId, (unsigned) e.attributeId,
                       modeStr, (unsigned) e.mode, (unsigned long long) e.operand);
            }
            break;
        }
        default:
            return MMIC_ERROR_NOT_IMPLEMENTED;
    };
    fflush(stdout);
    return MMIC_ERROR_OK;
}

void printHelp(void)
{
    #define X(a,b,c, d) fprintf(stderr, #a " :\t" b "\r\n");
    COMMAND_LIST
    #undef X
}

static const char * nameOrUnknown(const char * name)
{
    return (name != NULL) ? name : "Unknown";
}

uint8_t decodeAndPrintDeviceRequest(const uint8_t * buffer, size_t len)
{
    mmic_command_id_e opcode = INVALID_COMMAND_ID;
    const uint8_t * payload  = NULL;
    uint16_t payloadLen      = 0;
    uint8_t err = mmic_deserialize_packet(buffer, len, MMIC_HEADER_CMD, &opcode, &payload, &payloadLen);
    if (err != MMIC_ERROR_OK)
    {
        return err;
    }

    switch (opcode)
    {
        case cluster_command:
        {
            if (payloadLen < sizeof(clusterCommandHeader_t))
            {
                printf("\r\n[SoC] cluster_command: truncated\r\n");
                return MMIC_ERROR_INVALID_PACKET;
            }
            clusterCommandHeader_t hdr;
            memcpy(&hdr, payload, sizeof(hdr));
            printf("\r\n[SoC] Command %s.%s (endpoint %u, cluster 0x%04x, command 0x%02x, tid %u, %u byte(s) TLV params%s)\r\n",
                   nameOrUnknown(mmic_cluster_name(hdr.clusterId)),
                   nameOrUnknown(mmic_command_name(hdr.clusterId, hdr.commandId)), (unsigned) hdr.endpointId,
                   (unsigned) hdr.clusterId, (unsigned) hdr.commandId, (unsigned) hdr.transactionId,
                   (unsigned) (payloadLen - sizeof(hdr)),
                   (hdr.flags & MMIC_CLUSTER_COMMAND_FLAG_ANSWER_EXPECTED) ? ", answer expected" : "");
            break;
        }
        case attribute_changed:
        {
            if (payloadLen < sizeof(attributeChangedHeader_t))
            {
                printf("\r\n[SoC] attribute_changed: truncated\r\n");
                return MMIC_ERROR_INVALID_PACKET;
            }
            attributeChangedHeader_t hdr;
            memcpy(&hdr, payload, sizeof(hdr));
            printf("\r\n[SoC] Attribute %s.%s changed (endpoint %u, cluster 0x%04x, attribute 0x%04x, %u byte(s) TLV value)\r\n",
                   nameOrUnknown(mmic_cluster_name(hdr.clusterId)),
                   nameOrUnknown(mmic_attribute_name(hdr.clusterId, hdr.attributeId)), (unsigned) hdr.endpointId,
                   (unsigned) hdr.clusterId, (unsigned) hdr.attributeId, (unsigned) (payloadLen - sizeof(hdr)));
            break;
        }
        default:
            return MMIC_ERROR_NOT_IMPLEMENTED;
    }
    fflush(stdout);
    return MMIC_ERROR_OK;
}
#else
#include "mmic_forward.h"
#include <app/server/CommissioningWindowManager.h>
#include <app/server/Dnssd.h>
#include <app/server/Server.h>
#include <credentials/FabricTable.h>
#include <credentials/GroupDataProvider.h>
#include <crypto/CHIPCryptoPAL.h>
#include <lib/core/CHIPError.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/Span.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/CommissionableDataProvider.h>
#include <platform/ConnectivityManager.h>

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ThreadStackManager.h>
#include <openthread/dataset.h>
#include <openthread/instance.h>
#include <openthread/ip6.h>
#include <openthread/thread.h>

extern "C" otInstance * otGetInstance(void);
#endif


uint8_t encodeResponse(mmic_command_id_e id, void * response, size_t responseLen, uint8_t ** encodedPacket, size_t * packetSize)
{
    return mmic_serialize_packet(MMIC_HEADER_ANS, id, response, responseLen, encodedPacket, packetSize);
}

// Default application hook: no extra op codes handled.
__attribute__((weak)) uint8_t mmic_app_handle_command(mmic_command_id_e opcode, const uint8_t * payload, uint16_t payloadLen,
                                                     uint8_t ** response, size_t * packetSize)
{
    (void) opcode;
    (void) payload;
    (void) payloadLen;
    (void) response;
    (void) packetSize;
    return MMIC_ERROR_NOT_IMPLEMENTED;
}

uint8_t parseAndRunCommand(uint8_t * buffer, uint16_t len, uint8_t ** response, size_t * packetSize)
{
    VerifyOrReturnError(buffer != NULL && len != 0, MMIC_ERROR_INVALID_ARG);
    VerifyOrReturnError(response != NULL && packetSize != NULL, MMIC_ERROR_INVALID_ARG);

    mmic_command_id_e opcode = INVALID_COMMAND_ID;
    const uint8_t * payload  = NULL;
    uint16_t payloadLen      = 0;
    uint8_t derr = mmic_deserialize_packet(buffer, len, MMIC_HEADER_CMD, &opcode, &payload, &payloadLen);
    VerifyOrReturnError(derr == MMIC_ERROR_OK, derr);

    switch (opcode)
    {
        case ping:
            encodeResponse(ping, const_cast<char *>("pong"), sizeof("pong"), response, packetSize);
            break;
        case version:
            encodeResponse(version, const_cast<char *>(MMIC_VERSION_STRING), sizeof(MMIC_VERSION_STRING), response, packetSize);
            break;
        case matter_state: // To verify commissioning
            {
                matterState_t state;
                if (encodeMatterState(&state) == MMIC_ERROR_OK)
                {
                    encodeResponse(matter_state, &state, sizeof(matterState_t), response, packetSize);
                }
            }
            break;
        case decommission:
            {
                chip::DeviceLayer::PlatformMgr().LockChipStack();
                chip::Server::GetInstance().GetFabricTable().DeleteAllFabrics();
                uint8_t remaining = chip::Server::GetInstance().GetFabricTable().FabricCount();
                chip::DeviceLayer::PlatformMgr().UnlockChipStack();
                uint8_t status = (remaining == 0) ? 0 : 1;
                encodeResponse(decommission, &status, sizeof(status), response, packetSize);
            }
            break;
        case cluster_command:
            return mmic_forward_handle_request(payload, payloadLen, response, packetSize);
        default:
            return mmic_app_handle_command(opcode, payload, payloadLen, response, packetSize);
    };

    return MMIC_ERROR_OK;
}

uint8_t encodeMatterState(matterState_t * state)
{
    VerifyOrReturnError(state != nullptr, MMIC_ERROR_INVALID_ARG);

    memset(state, 0, sizeof(*state));

    auto & server = chip::Server::GetInstance();
    state->nbOfFabric              = server.GetFabricTable().FabricCount();
    state->commissioningWindowOpen = server.GetCommissioningWindowManager().IsCommissioningWindowOpen();

    // Proxy: operational DNS-SD advertising is up once at least one fabric exists.
    state->mdnsOperationalAdvertising = (state->nbOfFabric > 0);

    // Populate first-fabric identity for debugging.
    {
        auto & fabricTable = server.GetFabricTable();
        auto it = fabricTable.begin();
        if (it != fabricTable.end())
        {
            state->fabricIndex = it->GetFabricIndex();
            state->fabricId    = it->GetFabricId();
            state->nodeId      = it->GetNodeId();

            uint8_t cfidBuf[sizeof(uint64_t)];
            chip::MutableByteSpan cfidSpan(cfidBuf);
            if (it->GetCompressedFabricIdBytes(cfidSpan) == CHIP_NO_ERROR)
            {
                memcpy(state->compressedFabricId, cfidBuf, sizeof(state->compressedFabricId));
            }

            chip::Crypto::P256PublicKey rootPubKey;
            if (fabricTable.FetchRootPubkey(it->GetFabricIndex(), rootPubKey) == CHIP_NO_ERROR &&
                rootPubKey.Length() == sizeof(state->rootPublicKey))
            {
                memcpy(state->rootPublicKey, rootPubKey.ConstBytes(), sizeof(state->rootPublicKey));
            }
        }
    }

    if (state->commissioningWindowOpen)
    {
        auto & dnssd = chip::app::DnssdServer::Instance();
        state->mdnsCommissionableUdpPort = dnssd.GetSecuredPort();
        (void) dnssd.GetCommissionableInstanceName(state->mdnsCommissionableInstanceName,
                                                   sizeof(state->mdnsCommissionableInstanceName));

        auto * cdp = chip::DeviceLayer::GetCommissionableDataProvider();
        uint16_t discriminator = 0;
        if (cdp != nullptr && cdp->GetSetupDiscriminator(discriminator) == CHIP_NO_ERROR)
        {
            state->setupDiscriminator = discriminator;
        }
    }

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    state->threadAttached = chip::DeviceLayer::ConnectivityMgr().IsThreadAttached();

    otInstance * instance = otGetInstance();
    if (instance != nullptr)
    {
        chip::DeviceLayer::ThreadStackMgr().LockThreadStack();

        state->threadInterfaceUp = otIp6IsEnabled(instance);

        const otIp6Address * eid = otThreadGetMeshLocalEid(instance);
        if (eid != nullptr)
        {
            memcpy(state->threadMeshLocalEid, eid->mFields.m8, sizeof(state->threadMeshLocalEid));
        }

        otOperationalDataset dataset;
        if (otDatasetGetActive(instance, &dataset) == OT_ERROR_NONE)
        {
            if (dataset.mComponents.mIsPanIdPresent)
            {
                state->threadPanId = dataset.mPanId;
            }
            if (dataset.mComponents.mIsChannelPresent)
            {
                state->threadChannel = dataset.mChannel;
            }
            if (dataset.mComponents.mIsExtendedPanIdPresent)
            {
                memcpy(state->threadExtendedPanId, dataset.mExtendedPanId.m8, sizeof(state->threadExtendedPanId));
            }
            if (dataset.mComponents.mIsNetworkNamePresent)
            {
                strncpy(state->threadNetworkName, dataset.mNetworkName.m8, sizeof(state->threadNetworkName) - 1);
                state->threadNetworkName[sizeof(state->threadNetworkName) - 1] = '\0';
            }
        }

        chip::DeviceLayer::ThreadStackMgr().UnlockThreadStack();
    }
#endif // CHIP_DEVICE_CONFIG_ENABLE_THREAD

    return MMIC_ERROR_OK;
}
#endif // HOST_SIDE
