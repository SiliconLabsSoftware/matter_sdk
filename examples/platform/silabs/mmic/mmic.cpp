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

#if HOST_SIDE
#include <stdio.h>

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
    if(id >= INVALID_COMMAND_ID) 
    {
        return 1;
    }

    // For fixed-args commands defined via COMMAND_LIST, argsCnt/argsSize drive the wire size.
    // For variable-length commands (establish_subscription, commission) the caller passes size.
    const bool isVariableLen = (id == establish_subscription) || (id == commission);

    if(!isVariableLen && (commands[id].argsCnt == 0 && parameter != NULL))
    {
        return 1;
    }
    if(!isVariableLen && (commands[id].argsCnt != 0 && parameter == NULL))
    {
        return 1;
    }
    if(isVariableLen && (parameter == NULL || size == 0))
    {
        return 1;
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
        payloadSize = (uint16_t)(commands[id].argsSize);
    }

    const size_t frameSize = (size_t)MMIC_PACKET_OVERHEAD + payloadSize;
    if (frameSize > UINT16_MAX)
    {
        return 1;
    }

    uint8_t * workbuffer = (uint8_t *) malloc(frameSize);
    if (workbuffer == NULL)
    {
        return 2;
    }

    workbuffer[0] = MMIC_HEADER_CMD;
    mmic_write_length(workbuffer, (uint16_t)frameSize);
    workbuffer[MMIC_OFFSET_OP] = (uint8_t)id;

    if (payloadSize > 0)
    {
        memcpy(workbuffer + MMIC_OFFSET_PAYLOAD, parameter, payloadSize);
    }

    const uint16_t crc = crc16(workbuffer, (uint16_t)(frameSize - 2));
    memcpy(workbuffer + frameSize - 2, &crc, 2);

    *encodedPacket = workbuffer; // Warning must be freed by the caller
    *packetSize    = frameSize;
    return 0;
}

uint8_t decodeAndPrintResponse(uint8_t * buffer, size_t len)
{

    if(buffer == NULL || len == 0 )
    {
        return 1;
    }

    // Packet Integrity validation
    const uint16_t frameLen = mmic_read_length(buffer);
    if (buffer[0] != MMIC_HEADER_ANS
        || buffer[MMIC_OFFSET_OP] >= INVALID_COMMAND_ID
        || frameLen > len
        || frameLen < MMIC_PACKET_OVERHEAD)
    {
        return 2;
    }

    // CRC validation
    uint16_t crc=0;
    memcpy(&crc, buffer + (frameLen - 2), 2);
    if (crc != crc16(buffer, (uint16_t)(frameLen - 2)))
    {
        return 2;
    }

    switch(buffer[MMIC_OFFSET_OP])
    {
        case ping:
            printf("\r\n%s\r\n",(char *)buffer + MMIC_OFFSET_PAYLOAD );
            break;
        case version:
            printf("\r\n%s\r\n",(char *)buffer + MMIC_OFFSET_PAYLOAD );
            break;
        case matter_state:
            matterState_t state;
            memcpy(&state, buffer + MMIC_OFFSET_PAYLOAD, sizeof(matterState_t));
            printf("\r\nNumber of Fabrics: %d\r\nCommisionning Window Open:  %s\r\n",state.nbOfFabric, (state.commissioningWindowOpen)? "true" : "false" );
            printf("mDNS Operational Advertising: %s\r\n", state.mdnsOperationalAdvertising ? "true" : "false");
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
        case establish_subscription:
        case commission:
            printf("\r\n%s : %d\r\n", buffer[MMIC_OFFSET_OP] == 0 ? "Success":"Failure", buffer[MMIC_OFFSET_OP]);
            break;
        default:
            return 3; //Not implemented
    };
    fflush(stdout);
    return 0;
}

void printHelp(void)
{
    #define X(a,b,c, d) fprintf(stderr, #a " :\t" b "\r\n");
    COMMAND_LIST
    #undef X   
}
#else
#include "../subscription/SubscriptionManager.h"
#include <app/server/CommissioningWindowManager.h>
#include <app/server/Dnssd.h>
#include <app/server/Server.h>
#include <credentials/FabricTable.h>
#include <credentials/GroupDataProvider.h>
#include <crypto/CHIPCryptoPAL.h>
#include <lib/core/CHIPError.h>
#include <lib/support/Span.h>
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
    if(id >= INVALID_COMMAND_ID) 
    {
        return 1;
    }

    if(packetSize == NULL) 
    {
        return 1;
    }
    const size_t frameSize = (size_t)MMIC_PACKET_OVERHEAD + responseLen;
    if (frameSize > UINT16_MAX)
    {
        return 1;
    }

    uint8_t * workbuffer = (uint8_t *) malloc(frameSize);
    if (workbuffer == NULL)
    {
        return 2;
    }
    workbuffer[0]              = MMIC_HEADER_ANS;
    workbuffer[MMIC_OFFSET_OP] = (uint8_t)id;
    mmic_write_length(workbuffer, (uint16_t)frameSize);
    if (responseLen != 0)
    {
        memcpy(workbuffer + MMIC_OFFSET_PAYLOAD, response, responseLen);
    }
    const uint16_t crc = crc16(workbuffer, (uint16_t)(frameSize - 2));
    memcpy(workbuffer + frameSize - 2, &crc, 2);
    *encodedPacket = workbuffer;
    *packetSize    = frameSize;
    return 0;
}

// Local helper: perform local fabric injection using cert/key material supplied
// by the host over MMIC. Defined below parseAndRunCommand.
static uint8_t performCommission(const commissionArgs_t * args,
                                 const uint8_t * rcac, uint16_t rcacLen,
                                 const uint8_t * icac, uint16_t icacLen,
                                 const uint8_t * noc,  uint16_t nocLen);

uint8_t parseAndRunCommand(uint8_t * buffer, uint16_t len, uint8_t ** response, size_t * packetSize)
{
    if (buffer == NULL || len == 0 || response == NULL || packetSize == NULL)
    {
        return 1;
    }

    const uint16_t frameLen = mmic_read_length(buffer);
    if (frameLen > len || frameLen < MMIC_PACKET_OVERHEAD)
    {
        return 2;
    }

    uint16_t crc = 0;
    memcpy(&crc, buffer + (frameLen - 2), 2);
    // Packet Integrity validation
    if (buffer[0] != MMIC_HEADER_CMD
        || buffer[MMIC_OFFSET_OP] >= INVALID_COMMAND_ID
        || crc != crc16(buffer, (uint16_t)(frameLen - 2)))
    {
        return 2;
    }

    switch(buffer[MMIC_OFFSET_OP])
    {
        case ping:
            encodeResponse(ping, const_cast<char *>("pong"), sizeof("pong"), response, packetSize);
            break;
        case version:
            encodeResponse(ping, const_cast<char *>(MMIC_VERSION_STRING), sizeof(MMIC_VERSION_STRING), response, packetSize);
            break;
        case matter_state: // To verify commissioning
            {
                matterState_t state;
                if (encodeMatterState(&state) == 0)
                {
                    encodeResponse(matter_state, &state, sizeof(matterState_t), response, packetSize);
                }
            }
            break;
        case openCommissioning:
            {
                CHIP_ERROR err = chip::Server::GetInstance().GetCommissioningWindowManager().OpenBasicCommissioningWindow();
                uint8_t status = err == CHIP_NO_ERROR ? 0 : 1;
                encodeResponse(establish_subscription, &status, sizeof(status), response, packetSize);
            }
            break;
        case establish_subscription:
            {
                const uint16_t payloadLen = (uint16_t)(frameLen - MMIC_PACKET_OVERHEAD);
                if (payloadLen < sizeof(subscriptionArgs_t))
                {
                    return 2;
                }

                subscriptionArgs_t args;
                memcpy(&args, buffer + MMIC_OFFSET_PAYLOAD, sizeof(args));

                chip::Silabs::SubscriptionManager::Info info;
                info.fabricIndex = args.fabricIndex;
                info.nodeId      = args.nodeId;
                info.endpointId  = args.endpointId;
                info.clusterId   = args.clusterId;
                info.attributeId = args.attributeId;

                chip::Silabs::SubscriptionManager::Handle handle =
                    chip::Silabs::SubscriptionManager::kInvalidHandle;
                CHIP_ERROR err = chip::Silabs::SubscriptionManager::Instance().Subscribe(info, &handle);

                uint8_t status = err == CHIP_NO_ERROR ? 0 : 1;
                encodeResponse(establish_subscription, &status, sizeof(status), response, packetSize);
            }
            break;
        case commission:
            {
                const uint16_t payloadLen = (uint16_t)(frameLen - MMIC_PACKET_OVERHEAD);
                if (payloadLen < sizeof(commissionArgs_t))
                {
                    return 2;
                }

                commissionArgs_t hdr;
                memcpy(&hdr, buffer + MMIC_OFFSET_PAYLOAD, sizeof(hdr));

                const uint32_t certsTotal = (uint32_t)hdr.rcacLen + (uint32_t)hdr.icacLen + (uint32_t)hdr.nocLen;
                if (payloadLen != sizeof(commissionArgs_t) + certsTotal)
                {
                    return 2;
                }

                const uint8_t * certs = buffer + MMIC_OFFSET_PAYLOAD + sizeof(commissionArgs_t);
                const uint8_t * rcac  = certs;
                const uint8_t * icac  = certs + hdr.rcacLen;
                const uint8_t * noc   = icac + hdr.icacLen;

                uint8_t status = performCommission(&hdr, rcac, hdr.rcacLen,
                                                   (hdr.icacLen ? icac : nullptr), hdr.icacLen,
                                                   noc, hdr.nocLen);
                encodeResponse(commission, &status, sizeof(status), response, packetSize);
            }
            break;
        default:
            return 3; //Not implemented
    };


    return 0;
}



uint8_t encodeMatterState(matterState_t * state)
{
    if(state == nullptr)
    {
        return 1;
    }

    memset(state, 0, sizeof(*state));

    auto & server = chip::Server::GetInstance();
    state->nbOfFabric              = server.GetFabricTable().FabricCount();
    state->commissioningWindowOpen = server.GetCommissioningWindowManager().IsCommissioningWindowOpen();

    // Proxy: operational DNS-SD advertising is up once at least one fabric exists.
    state->mdnsOperationalAdvertising = (state->nbOfFabric > 0);

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

    return 0;
}
uint8_t establishSubscription()
{
    return 0;
}
uint8_t getSubscriptionsInfo()
{
    return 0;
}

// Inject a fabric into the device using pre-built certs, opkey and IPK
// supplied over MMIC by the host. Returns 0 on success, non-zero on any
// failure. Must not be invoked from within a CHIP-stack callback (grabs
// the stack lock).
static uint8_t performCommission(const commissionArgs_t * args,
                                 const uint8_t * rcac, uint16_t rcacLen,
                                 const uint8_t * icac, uint16_t icacLen,
                                 const uint8_t * noc,  uint16_t nocLen)
{
    using namespace chip;
    using namespace chip::Credentials;
    using namespace chip::Crypto;

    if (args == nullptr || rcac == nullptr || rcacLen == 0 || noc == nullptr || nocLen == 0)
    {
        return 1;
    }

    // Rebuild the operational keypair from the wire material (uncompressed pub
    // point 0x04||X||Y then the private scalar) into a P256SerializedKeypair
    // and Deserialize() into a P256Keypair suitable for FabricTable.
    P256SerializedKeypair serialized;
    if (serialized.Capacity() < (MMIC_COMMISSION_OPKEY_PUB_LEN + MMIC_COMMISSION_OPKEY_PRIV_LEN))
    {
        return 2;
    }
    memcpy(serialized.Bytes(), args->opkeyPub, MMIC_COMMISSION_OPKEY_PUB_LEN);
    memcpy(serialized.Bytes() + MMIC_COMMISSION_OPKEY_PUB_LEN,
           args->opkeyPriv, MMIC_COMMISSION_OPKEY_PRIV_LEN);
    serialized.SetLength(MMIC_COMMISSION_OPKEY_PUB_LEN + MMIC_COMMISSION_OPKEY_PRIV_LEN);

    P256Keypair opKey;
    if (opKey.Deserialize(serialized) != CHIP_NO_ERROR)
    {
        return 3;
    }

    DeviceLayer::PlatformMgr().LockChipStack();

    FabricTable & fabricTable = Server::GetInstance().GetFabricTable();

    ByteSpan rcacSpan(rcac, rcacLen);
    ByteSpan icacSpan;
    if (icac != nullptr && icacLen > 0)
    {
        icacSpan = ByteSpan(icac, icacLen);
    }
    ByteSpan nocSpan(noc, nocLen);

    FabricIndex newFabricIndex = kUndefinedFabricIndex;
    CHIP_ERROR err = fabricTable.AddNewPendingTrustedRootCert(rcacSpan);
    if (err != CHIP_NO_ERROR)
    {
        DeviceLayer::PlatformMgr().UnlockChipStack();
        return 4;
    }

    err = fabricTable.AddNewPendingFabricWithProvidedOpKey(nocSpan, icacSpan, args->vendorId,
                                                           &opKey, /*isExistingOpKeyExternallyOwned=*/false,
                                                           &newFabricIndex);
    if (err != CHIP_NO_ERROR)
    {
        fabricTable.RevertPendingFabricData();
        DeviceLayer::PlatformMgr().UnlockChipStack();
        return 5;
    }

    err = fabricTable.CommitPendingFabricData();
    if (err != CHIP_NO_ERROR)
    {
        fabricTable.RevertPendingFabricData();
        DeviceLayer::PlatformMgr().UnlockChipStack();
        return 6;
    }

    // Install the IPK for the newly committed fabric. Compressed fabric ID
    // is needed as the group key context; the FabricTable computes it from
    // the committed root cert.
    const FabricInfo * fabricInfo = fabricTable.FindFabricWithIndex(newFabricIndex);
    if (fabricInfo == nullptr)
    {
        DeviceLayer::PlatformMgr().UnlockChipStack();
        return 7;
    }

    uint8_t compressedFabricIdBuf[sizeof(uint64_t)];
    MutableByteSpan compressedFabricIdSpan(compressedFabricIdBuf);
    err = fabricInfo->GetCompressedFabricIdBytes(compressedFabricIdSpan);
    if (err != CHIP_NO_ERROR)
    {
        DeviceLayer::PlatformMgr().UnlockChipStack();
        return 8;
    }

    GroupDataProvider * groupDataProvider = GetGroupDataProvider();
    if (groupDataProvider == nullptr)
    {
        DeviceLayer::PlatformMgr().UnlockChipStack();
        return 9;
    }

    ByteSpan ipkSpan(args->ipk, MMIC_COMMISSION_IPK_LEN);
    err = SetSingleIpkEpochKey(groupDataProvider, newFabricIndex, ipkSpan, compressedFabricIdSpan);
    if (err != CHIP_NO_ERROR)
    {
        DeviceLayer::PlatformMgr().UnlockChipStack();
        return 10;
    }

    // Restart operational DNS-SD so the new fabric shows up on the network.
    (void) app::DnssdServer::Instance().AdvertiseOperational();

    DeviceLayer::PlatformMgr().UnlockChipStack();
    return 0;
}
#endif // HOST_SIDE