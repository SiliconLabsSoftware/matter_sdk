/*******************************************************************************
 * @file
 * @brief MMIC request handlers specific to the wake-on-matter application.
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
#include "mmic_wake_on_matter.h"

#include "../subscription/SubscriptionManager.h"
#include "WakeUpMgr.h"
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

#include <string.h>

// Local helper: perform local fabric injection using cert/key material supplied
// by the host over MMIC. Defined below mmic_app_handle_command.
static uint8_t performCommission(const commissionArgs_t * args,
                                 const uint8_t * rcac, uint16_t rcacLen,
                                 const uint8_t * icac, uint16_t icacLen,
                                 const uint8_t * noc,  uint16_t nocLen);

// SubscriptionManager delegate: logs lifecycle + attribute reports to the
// device console. Attaches lazily on the first establish_subscription call.
namespace {

mmic_subscription_cb_t gSubscriptionCallback = nullptr;

class MmicSubscriptionDelegate : public chip::Silabs::SubscriptionManager::Delegate
{
public:
    void OnAttributeData(chip::Silabs::SubscriptionManager::Handle handle,
                         const chip::Silabs::SubscriptionManager::Info & info,
                         const chip::app::ConcreteDataAttributePath & path,
                         chip::TLV::TLVReader * data,
                         const chip::app::StatusIB & status) override
    {
        (void) data;
        ChipLogProgress(NotSpecified,
                        "mmic sub[%u] report: node=0x" ChipLogFormatX64
                        " endpoint=%u cluster=" ChipLogFormatMEI " attr=" ChipLogFormatMEI
                        " status=0x%02x",
                        handle, ChipLogValueX64(info.nodeId), path.mEndpointId,
                        ChipLogValueMEI(path.mClusterId), ChipLogValueMEI(path.mAttributeId),
                        static_cast<unsigned>(status.mStatus));
        if (gSubscriptionCallback != nullptr)
        {
            uint64_t value = 0;
            if (data != nullptr)
            {
                switch (data->GetType())
                {
                case chip::TLV::kTLVType_Boolean: {
                    bool b = false;
                    if (data->Get(b) == CHIP_NO_ERROR) { value = b ? 1u : 0u; }
                    break;
                }
                case chip::TLV::kTLVType_UnsignedInteger: {
                    uint64_t u = 0;
                    if (data->Get(u) == CHIP_NO_ERROR) { value = u; }
                    break;
                }
                case chip::TLV::kTLVType_SignedInteger: {
                    int64_t s = 0;
                    if (data->Get(s) == CHIP_NO_ERROR) { value = static_cast<uint64_t>(s); }
                    break;
                }
                default:
                    break;
                }
            }
            gSubscriptionCallback(path.mEndpointId, path.mClusterId, path.mAttributeId, value);
        }
    }
    void OnSubscriptionEstablished(chip::Silabs::SubscriptionManager::Handle handle,
                                   const chip::Silabs::SubscriptionManager::Info & info,
                                   chip::SubscriptionId subscriptionId) override
    {
        ChipLogProgress(NotSpecified,
                        "mmic sub[%u] established (subId=" ChipLogFormatX64 ") node=0x" ChipLogFormatX64,
                        handle, ChipLogValueX64(subscriptionId), ChipLogValueX64(info.nodeId));
    }
    void OnError(chip::Silabs::SubscriptionManager::Handle handle,
                 const chip::Silabs::SubscriptionManager::Info & info, CHIP_ERROR error) override
    {
        (void) info;
        ChipLogError(NotSpecified, "mmic sub[%u] error: %" CHIP_ERROR_FORMAT, handle, error.Format());
    }
    void OnSubscriptionTerminated(chip::Silabs::SubscriptionManager::Handle handle,
                                  const chip::Silabs::SubscriptionManager::Info & info) override
    {
        ChipLogProgress(NotSpecified, "mmic sub[%u] terminated (node=0x" ChipLogFormatX64 ")",
                        handle, ChipLogValueX64(info.nodeId));
    }
};

MmicSubscriptionDelegate gSubscriptionDelegate;
bool gSubscriptionDelegateInstalled = false;

void ensureSubscriptionDelegateInstalled()
{
    if (!gSubscriptionDelegateInstalled)
    {
        chip::Silabs::SubscriptionManager::Instance().SetDelegate(&gSubscriptionDelegate);
        gSubscriptionDelegateInstalled = true;
    }
}
} // namespace

void mmic_set_subscription_callback(mmic_subscription_cb_t cb)
{
    gSubscriptionCallback = cb;
}

uint8_t mmic_app_handle_command(mmic_command_id_e opcode, const uint8_t * payload, uint16_t payloadLen,
                                uint8_t ** response, size_t * packetSize)
{
    VerifyOrReturnError(response != NULL && packetSize != NULL, MMIC_ERROR_INVALID_ARG);

    switch (opcode)
    {
        case establish_subscription:
            {
                VerifyOrReturnError(payloadLen >= sizeof(subscriptionArgs_t), MMIC_ERROR_INVALID_PACKET);

                subscriptionArgs_t args;
                memcpy(&args, payload, sizeof(args));

                chip::Silabs::SubscriptionManager::Info info;
                info.fabricIndex = args.fabricIndex;
                info.nodeId      = args.nodeId;
                info.endpointId  = args.endpointId;
                info.clusterId   = args.clusterId;
                info.attributeId = args.attributeId;

                ensureSubscriptionDelegateInstalled();

                chip::Silabs::SubscriptionManager::Handle handle =
                    chip::Silabs::SubscriptionManager::kInvalidHandle;

                chip::DeviceLayer::PlatformMgr().LockChipStack();
                CHIP_ERROR err = chip::Silabs::SubscriptionManager::Instance().Subscribe(info, &handle);
                chip::DeviceLayer::PlatformMgr().UnlockChipStack();

                subscriptionEstablishResp_t resp;
                resp.status = (err == CHIP_NO_ERROR) ? 0 : 1;
                resp.handle = handle;
                encodeResponse(establish_subscription, &resp, sizeof(resp), response, packetSize);
            }
            break;
        case subscription_info:
            {
                auto & mgr = chip::Silabs::SubscriptionManager::Instance();

                subscriptionEntry_t entries[MMIC_SUBSCRIPTION_MAX_ENTRIES];
                uint8_t count = 0;

                chip::DeviceLayer::PlatformMgr().LockChipStack();
                for (uint8_t h = 0;
                     h < chip::Silabs::SubscriptionManager::kMaxSubscriptions &&
                     count < MMIC_SUBSCRIPTION_MAX_ENTRIES;
                     ++h)
                {
                    const auto * info = mgr.GetInfo(h);
                    if (info == nullptr) continue;
                    entries[count].handle      = h;
                    entries[count].fabricIndex = info->fabricIndex;
                    entries[count].nodeId      = info->nodeId;
                    entries[count].endpointId  = info->endpointId;
                    entries[count].clusterId   = info->clusterId;
                    entries[count].attributeId = info->attributeId;
                    ++count;
                }
                chip::DeviceLayer::PlatformMgr().UnlockChipStack();

                uint8_t payloadBuf[1 + MMIC_SUBSCRIPTION_MAX_ENTRIES * sizeof(subscriptionEntry_t)];
                payloadBuf[0] = count;
                if (count > 0)
                {
                    memcpy(&payloadBuf[1], entries, (size_t) count * sizeof(subscriptionEntry_t));
                }
                encodeResponse(subscription_info, payloadBuf,
                               1 + (size_t) count * sizeof(subscriptionEntry_t),
                               response, packetSize);
            }
            break;
        case commission:
            {
                VerifyOrReturnError(payloadLen >= sizeof(commissionArgs_t), MMIC_ERROR_INVALID_PACKET);

                commissionArgs_t hdr;
                memcpy(&hdr, payload, sizeof(hdr));

                const uint32_t certsTotal = (uint32_t) hdr.rcacLen + (uint32_t) hdr.icacLen + (uint32_t) hdr.nocLen;
                VerifyOrReturnError(payloadLen == sizeof(commissionArgs_t) + certsTotal, MMIC_ERROR_INVALID_PACKET);

                const uint8_t * certs = payload + sizeof(commissionArgs_t);
                const uint8_t * rcac  = certs;
                const uint8_t * icac  = certs + hdr.rcacLen;
                const uint8_t * noc   = icac + hdr.icacLen;

                uint8_t status = performCommission(&hdr, rcac, hdr.rcacLen,
                                                   (hdr.icacLen ? icac : nullptr), hdr.icacLen,
                                                   noc, hdr.nocLen);
                encodeResponse(commission, &status, sizeof(status), response, packetSize);
            }
            break;
        case addWakeUp:
            {
                VerifyOrReturnError(payloadLen >= sizeof(wakeUpEntry_t), MMIC_ERROR_INVALID_PACKET);

                wakeUpEntry_t entry;
                memcpy(&entry, payload, sizeof(entry));

                CHIP_ERROR err = chip::Silabs::WakeUpMgr::Instance().SetWakeUpTrigger(
                    entry.clusterId, entry.attributeId,
                    static_cast<chip::Silabs::WakeUpMatchMode>(entry.mode), entry.operand);
                uint8_t status = (err == CHIP_NO_ERROR) ? MMIC_ERROR_OK : MMIC_ERROR_INVALID_ARG;
                encodeResponse(addWakeUp, &status, sizeof(status), response, packetSize);
            }
            break;
        case removeWakeUp:
            {
                VerifyOrReturnError(payloadLen >= sizeof(wakeUpRemoveArgs_t), MMIC_ERROR_INVALID_PACKET);

                wakeUpRemoveArgs_t args;
                memcpy(&args, payload, sizeof(args));

                CHIP_ERROR err = chip::Silabs::WakeUpMgr::Instance().RemoveWakeUpTrigger(args.clusterId, args.attributeId);
                uint8_t status = (err == CHIP_NO_ERROR) ? MMIC_ERROR_OK : MMIC_ERROR_INVALID_ARG;
                encodeResponse(removeWakeUp, &status, sizeof(status), response, packetSize);
            }
            break;
        case wakeUpList:
            {
                // Enforce shared upper bound between manager and wire format at compile time.
                static_assert(chip::Silabs::WakeUpMgr::kMaxTriggers <= MMIC_WAKEUP_MAX_ENTRIES,
                              "MMIC_WAKEUP_MAX_ENTRIES must cover WakeUpMgr::kMaxTriggers");
                static_assert(sizeof(wakeUpEntry_t) == sizeof(chip::Silabs::WakeUpTrigger),
                              "wire wakeUpEntry_t must match WakeUpTrigger layout");

                auto triggers = chip::Silabs::WakeUpMgr::Instance().GetWakeUpTriggers();
                const uint8_t count = static_cast<uint8_t>(
                    (triggers.size() > MMIC_WAKEUP_MAX_ENTRIES) ? MMIC_WAKEUP_MAX_ENTRIES : triggers.size());

                uint8_t payloadBuf[1 + MMIC_WAKEUP_MAX_ENTRIES * sizeof(wakeUpEntry_t)];
                payloadBuf[0] = count;
                for (uint8_t i = 0; i < count; ++i)
                {
                    wakeUpEntry_t entry;
                    entry.clusterId   = triggers[i].clusterId;
                    entry.attributeId = triggers[i].attributeId;
                    entry.operand     = triggers[i].operand;
                    entry.mode        = triggers[i].mode;
                    memcpy(&payloadBuf[1 + i * sizeof(wakeUpEntry_t)], &entry, sizeof(entry));
                }
                encodeResponse(wakeUpList, payloadBuf,
                               1 + static_cast<size_t>(count) * sizeof(wakeUpEntry_t),
                               response, packetSize);
            }
            break;
        default:
            return MMIC_ERROR_NOT_IMPLEMENTED;
    };

    return MMIC_ERROR_OK;
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

    VerifyOrReturnError(args != nullptr, 1);
    VerifyOrReturnError(rcac != nullptr && rcacLen != 0, 1);
    VerifyOrReturnError(noc != nullptr && nocLen != 0, 1);

    // Rebuild the operational keypair from the wire material (uncompressed pub
    // point 0x04||X||Y then the private scalar) into a P256SerializedKeypair
    // and Deserialize() into a P256Keypair suitable for FabricTable.
    P256SerializedKeypair serialized;
    VerifyOrReturnError(serialized.Capacity() >= (MMIC_COMMISSION_OPKEY_PUB_LEN + MMIC_COMMISSION_OPKEY_PRIV_LEN), 2);
    memcpy(serialized.Bytes(), args->opkeyPub, MMIC_COMMISSION_OPKEY_PUB_LEN);
    memcpy(serialized.Bytes() + MMIC_COMMISSION_OPKEY_PUB_LEN,
           args->opkeyPriv, MMIC_COMMISSION_OPKEY_PRIV_LEN);
    VerifyOrReturnError(CHIP_NO_ERROR ==
                            serialized.SetLength(MMIC_COMMISSION_OPKEY_PUB_LEN + MMIC_COMMISSION_OPKEY_PRIV_LEN),
                        3);

    P256Keypair opKey;
    VerifyOrReturnError(opKey.Deserialize(serialized) == CHIP_NO_ERROR, 3);

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
    VerifyOrReturnError(err == CHIP_NO_ERROR, 4, DeviceLayer::PlatformMgr().UnlockChipStack());

    err = fabricTable.AddNewPendingFabricWithProvidedOpKey(nocSpan, icacSpan, args->vendorId,
                                                           &opKey, /*isExistingOpKeyExternallyOwned=*/false,
                                                           &newFabricIndex);
    VerifyOrReturnError(err == CHIP_NO_ERROR, 5,
                        fabricTable.RevertPendingFabricData(); DeviceLayer::PlatformMgr().UnlockChipStack());

    err = fabricTable.CommitPendingFabricData();
    VerifyOrReturnError(err == CHIP_NO_ERROR, 6,
                        fabricTable.RevertPendingFabricData(); DeviceLayer::PlatformMgr().UnlockChipStack());

    // Install the IPK for the newly committed fabric. Compressed fabric ID
    // is needed as the group key context; the FabricTable computes it from
    // the committed root cert.
    const FabricInfo * fabricInfo = fabricTable.FindFabricWithIndex(newFabricIndex);
    VerifyOrReturnError(fabricInfo != nullptr, 7, DeviceLayer::PlatformMgr().UnlockChipStack());

    uint8_t compressedFabricIdBuf[sizeof(uint64_t)];
    MutableByteSpan compressedFabricIdSpan(compressedFabricIdBuf);
    err = fabricInfo->GetCompressedFabricIdBytes(compressedFabricIdSpan);
    VerifyOrReturnError(err == CHIP_NO_ERROR, 8, DeviceLayer::PlatformMgr().UnlockChipStack());

    GroupDataProvider * groupDataProvider = GetGroupDataProvider();
    VerifyOrReturnError(groupDataProvider != nullptr, 9, DeviceLayer::PlatformMgr().UnlockChipStack());

    ByteSpan ipkSpan(args->ipk, MMIC_COMMISSION_IPK_LEN);
    err = SetSingleIpkEpochKey(groupDataProvider, newFabricIndex, ipkSpan, compressedFabricIdSpan);
    VerifyOrReturnError(err == CHIP_NO_ERROR, 10, DeviceLayer::PlatformMgr().UnlockChipStack());

    // Restart operational DNS-SD so the new fabric shows up on the network.
    (void) app::DnssdServer::Instance().AdvertiseOperational();

    DeviceLayer::PlatformMgr().UnlockChipStack();
    return MMIC_ERROR_OK;
}
