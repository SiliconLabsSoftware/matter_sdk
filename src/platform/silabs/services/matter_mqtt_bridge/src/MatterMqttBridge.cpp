/*
 *
 *    Copyright (c) 2026 Project CHIP Authors
 *    All rights reserved.
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

#include "MatterMqttBridge.h"
#include "MatterMqttLog.h"
#include "MatterMqttTopic.h"
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>

#include <app/util/attribute-storage.h>
#include <app/util/endpoint-config-api.h>
#include <lib/support/Span.h>
#include <platform/silabs/services/mqtt_client.h>

#include <atomic>
#include <cinttypes>
#include <cstring>

namespace MatterMqttBridge {
namespace {

// --- File-local types (bridge TU only; not published in headers) ---

/**
 * Shared bridge slot pool (depth matches MqttClient work queue): async attribute publish buffers on the MQTT thread,
 * or inbound command + invoke TLV held until Matter-thread invoke completes.
 */
// Slot pool: acquire on the MQTT service thread only; release on MQTT or Matter thread (idempotent).
struct BridgeSlot
{
    std::atomic<bool> inUse{ false };
    std::atomic<bool> commandHeld{ false };
    char topic[MATTER_MQTT_MAX_TOPIC_LEN]{};
    char payload[MATTER_MQTT_MAX_PAYLOAD_LEN]{};
    size_t payloadLen  = 0;
    MatterMqttTopic::ParsedCommandTopic command;
    uint8_t invokeTlv[MATTER_MQTT_MAX_INVOKE_TLV]{};
    size_t invokeTlvLen = 0;
};

// --- Constants ---

constexpr size_t kBridgeSlotCount = 4;

// --- Static state (Init/Shutdown on app/MQTT paths; invoke on Matter thread) ---

MatterMqttJsonCodec sCodec;
chip::DeviceLayer::Silabs::MqttClient * sClient = nullptr;

MatterMqttBridgeConfig sConfig;

// Subscribe stores the topic pointer until the MQTT thread handles it, so each endpoint keeps its own buffer.
char sCommandSubscriptionTopics[MAX_ENDPOINT_COUNT][MATTER_MQTT_MAX_TOPIC_LEN] = {};

uint32_t sAttributePublishSeq = 0;
BridgeSlot sBridgeSlots[kBridgeSlotCount];

// --- Forward declarations ---

BridgeSlot * AcquireBridgeSlot();
void ReleaseBridgeSlot(BridgeSlot * slot);
void OnAttributePublishDone(CHIP_ERROR result, void * context);

void ReportWorkHandler(const uint8_t * data, size_t len);

CHIP_ERROR EnqueueReport(const chip::app::ConcreteAttributePath & path);

void HandleInboundCommandSlot(intptr_t arg);
bool OnInboundMessage(chip::CharSpan topic, chip::ByteSpan payload);

void SubscribeCommands();
void InitOnMatterThread(intptr_t arg);

// --- Bridge slot pool (MQTT thread; publish and inbound command share depth) ---

BridgeSlot * AcquireBridgeSlot()
{
    for (size_t i = 0; i < kBridgeSlotCount; ++i)
    {
        bool expected = false;
        if (sBridgeSlots[i].inUse.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
            sBridgeSlots[i].commandHeld.store(false, std::memory_order_relaxed);
            sBridgeSlots[i].payloadLen   = 0;
            sBridgeSlots[i].invokeTlvLen = 0;
            return &sBridgeSlots[i];
        }
    }
    return nullptr;
}

void ReleaseBridgeSlot(BridgeSlot * slot)
{
    if (slot != nullptr)
    {
        slot->commandHeld.store(false, std::memory_order_release);
        slot->inUse.store(false, std::memory_order_release);
    }
}

void OnAttributePublishDone(CHIP_ERROR result, void * context)
{
    ReleaseBridgeSlot(static_cast<BridgeSlot *>(context));
    if (result != CHIP_NO_ERROR)
    {
        MATTER_MQTT_LOG_ERROR("attribute publish async failed err=%" CHIP_ERROR_FORMAT, result.Format());
    }
}

/** Runs on the MQTT service thread: build attribute report topic + JSON payload, then Publish. */
void ReportWorkHandler(const uint8_t * data, size_t len)
{
    ReportWork work{};
    DeviceMqttProfile * profile = sConfig.profile;
    VerifyOrReturn(sClient != nullptr && sConfig.filterRegistry != nullptr && profile != nullptr);
    VerifyOrReturn(profile->IsMqttDeviceIdKnown());
    VerifyOrReturn(len == sizeof(ReportWork));
    memcpy(&work, data, sizeof(work));

    const MatterMqttClusterFilter * clusterFilter = sConfig.filterRegistry->FindByClusterId(work.clusterId);
    const MatterMqttAttributeFilter * attr =
        sConfig.filterRegistry->FindAttribute(clusterFilter, work.attributeId);
    VerifyOrReturn(clusterFilter != nullptr && attr != nullptr);
    VerifyOrReturn(work.value[0] != '\0');

    BridgeSlot * slot = AcquireBridgeSlot();
    if (slot == nullptr)
    {
        MATTER_MQTT_LOG_ERROR("attribute report skipped: bridge slot pool full");
        return;
    }

    if (MatterMqttTopic::BuildAttributeTopic(profile->GetMqttDeviceNodeId(), work.endpoint, work.clusterId,
                                             work.attributeId, slot->topic, sizeof(slot->topic)) != CHIP_NO_ERROR)
    {
        ReleaseBridgeSlot(slot);
        return;
    }

    if (sCodec.EncodeReport(chip::CharSpan::fromCharString(work.value), slot->payload, sizeof(slot->payload)) != CHIP_NO_ERROR)
    {
        ReleaseBridgeSlot(slot);
        return;
    }
    slot->payloadLen = strlen(slot->payload);
    const chip::ByteSpan payload(reinterpret_cast<uint8_t *>(slot->payload), slot->payloadLen);
    const auto qos            = static_cast<chip::DeviceLayer::Silabs::MqttQoS>(sConfig.reportQos);
    const uint32_t publishSeq = ++sAttributePublishSeq;
    MATTER_MQTT_LOG("attribute publish #%" PRIu32 " topic=%s payload=%s", publishSeq, slot->topic, slot->payload);

    const CHIP_ERROR publishErr =
        sClient->Publish(slot->topic, payload, qos, sConfig.reportRetained, OnAttributePublishDone, slot);
    if (publishErr != CHIP_NO_ERROR)
    {
        MATTER_MQTT_LOG_ERROR("attribute publish #%" PRIu32 " queue failed topic=%s", publishSeq, slot->topic);
        ReleaseBridgeSlot(slot);
    }
}

// Matter thread: read attribute, then hand a ReportWork blob to the MQTT service thread.
CHIP_ERROR EnqueueReport(const chip::app::ConcreteAttributePath & path)
{
    VerifyOrReturnError(sClient != nullptr && sClient->IsConnected(), CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(sConfig.reader != nullptr && sConfig.filterRegistry != nullptr, CHIP_ERROR_INCORRECT_STATE);

    const MatterMqttClusterFilter * clusterFilter = sConfig.filterRegistry->FindByClusterId(path.mClusterId);
    VerifyOrReturnError(clusterFilter != nullptr, CHIP_ERROR_NOT_FOUND);
    VerifyOrReturnError(sConfig.filterRegistry->FindAttribute(clusterFilter, path.mAttributeId) != nullptr,
                        CHIP_ERROR_NOT_FOUND);

    ReportWork work{};
    work.endpoint    = path.mEndpointId;
    work.clusterId   = path.mClusterId;
    work.attributeId = path.mAttributeId;

    const CHIP_ERROR readErr = sConfig.reader->ReadAsString(path, work.value, sizeof(work.value));
    if (readErr != CHIP_NO_ERROR)
    {
        MATTER_MQTT_LOG_ERROR("attribute read failed ep=%u err=%" CHIP_ERROR_FORMAT, static_cast<unsigned>(path.mEndpointId),
                              readErr.Format());
        return readErr;
    }

    const CHIP_ERROR schedErr = sClient->ScheduleWork(ReportWorkHandler, &work, sizeof(work));
    if (schedErr != CHIP_NO_ERROR)
    {
        MATTER_MQTT_LOG_ERROR("attribute report schedule failed ep=%u err=%" CHIP_ERROR_FORMAT,
                              static_cast<unsigned>(path.mEndpointId), schedErr.Format());
        return schedErr;
    }

    MATTER_MQTT_LOG_DETAIL("attribute report queued ep=%u attr=" ChipLogFormatMEI, static_cast<unsigned>(path.mEndpointId),
                           ChipLogValueMEI(path.mAttributeId));
    return CHIP_NO_ERROR;
}

// Matter thread: TLV invoke from slot-owned JSON command args.
void HandleInboundCommandSlot(intptr_t arg)
{
    const size_t index = static_cast<size_t>(arg);
    VerifyOrReturn(index < kBridgeSlotCount);

    BridgeSlot * slot = &sBridgeSlots[index];
    VerifyOrReturn(slot->inUse.load(std::memory_order_acquire) && slot->commandHeld.load(std::memory_order_acquire));
    VerifyOrReturn(sConfig.injector != nullptr);

    const MatterMqttTopic::ParsedCommandTopic & command = slot->command;
    MATTER_MQTT_LOG("inbound command ep=%u cluster=" ChipLogFormatMEI " cmd=" ChipLogFormatMEI,
                    static_cast<unsigned>(command.endpoint), ChipLogValueMEI(command.clusterId),
                    ChipLogValueMEI(command.commandId));

    if (sConfig.injector->Invoke(command, slot->invokeTlv, slot->invokeTlvLen) != CHIP_NO_ERROR)
    {
        MATTER_MQTT_LOG_ERROR("command inject failed");
    }
    ReleaseBridgeSlot(slot);
}

// MQTT thread: parse command topic + JSON payload; may return false when topic is not for this device/command contract.
bool OnInboundMessage(chip::CharSpan topic, chip::ByteSpan payload)
{
    DeviceMqttProfile * profile = sConfig.profile;
    VerifyOrReturnError(sConfig.filterRegistry != nullptr && sConfig.injector != nullptr && profile != nullptr, false);
    VerifyOrReturnError(profile->IsMqttDeviceIdKnown(), false);

    // --- Topic: parse path ids; a topic for another node is not ours, so return false ---
    MatterMqttTopic::ParsedCommandTopic parsed;
    if (MatterMqttTopic::ParseCommandTopic(topic, parsed) != CHIP_NO_ERROR ||
        parsed.nodeId != profile->GetMqttDeviceNodeId())
    {
        return false;
    }

    const chip::CommandId commandId = parsed.commandId;

    if (!ValidateParsedCommand(*profile, sConfig.filterRegistry, parsed))
    {
        return true;
    }

    // --- JSON payload -> ArgsView name/value pairs ---
    MatterMqttCodecTypes::ArgsView args;
    if (sCodec.DecodeArgs(payload, args) != CHIP_NO_ERROR)
    {
        MATTER_MQTT_LOG_ERROR("command rejected cmd=" ChipLogFormatMEI " (args)", ChipLogValueMEI(commandId));
        return true;
    }

    BridgeSlot * slot = AcquireBridgeSlot();
    if (slot == nullptr)
    {
        MATTER_MQTT_LOG_ERROR("command rejected cmd=" ChipLogFormatMEI " (busy)", ChipLogValueMEI(commandId));
        return true;
    }

    if (sConfig.injector->EncodeInvokeTlv(parsed.clusterId, commandId, &args, slot->invokeTlv, sizeof(slot->invokeTlv),
                                          slot->invokeTlvLen) != CHIP_NO_ERROR)
    {
        ReleaseBridgeSlot(slot);
        MATTER_MQTT_LOG_ERROR("command rejected cmd=" ChipLogFormatMEI " (args_pack)", ChipLogValueMEI(commandId));
        return true;
    }

    slot->commandHeld.store(true, std::memory_order_release);
    slot->command = parsed;

    // Cross to Matter thread on CHIP stack; InvokeCommand must not run on the MQTT service thread.
    const size_t slotIndex = static_cast<size_t>(slot - sBridgeSlots);
    if (chip::DeviceLayer::PlatformMgr().ScheduleWork(HandleInboundCommandSlot, slotIndex) != CHIP_NO_ERROR)
    {
        ReleaseBridgeSlot(slot);
        MATTER_MQTT_LOG_ERROR("command rejected cmd=" ChipLogFormatMEI " (schedule)", ChipLogValueMEI(commandId));
        return true;
    }
    return true;
}

// Subscribe to matter/v1/{deviceId}/ep/{endpoint}/cluster/+/cmd/+ for each registered endpoint except root.
void SubscribeCommands()
{
    DeviceMqttProfile * profile = sConfig.profile;
    VerifyOrReturn(sClient != nullptr && profile != nullptr);
    VerifyOrReturn(sClient->IsConnected());

    if (profile->ResolveMqttDeviceNodeId() != CHIP_NO_ERROR)
    {
        MATTER_MQTT_LOG("MQTT connected; waiting for Matter fabric NodeId");
        return;
    }

    const uint16_t endpointCount = emberAfEndpointCount();
    const auto qos               = static_cast<chip::DeviceLayer::Silabs::MqttQoS>(sConfig.commandQos);
    for (uint16_t index = 0; index < endpointCount && index < MAX_ENDPOINT_COUNT; ++index)
    {
        const chip::EndpointId endpoint = emberAfEndpointFromIndex(index);
        if (endpoint == chip::kInvalidEndpointId || endpoint == chip::kRootEndpointId)
        {
            continue;
        }

        char * topic = sCommandSubscriptionTopics[index];
        if (MatterMqttTopic::BuildCommandSubscriptionTopic(profile->GetMqttDeviceNodeId(), endpoint, topic,
                                                           MATTER_MQTT_MAX_TOPIC_LEN) != CHIP_NO_ERROR)
        {
            MATTER_MQTT_LOG_ERROR("command subscription topic build failed ep=%u", static_cast<unsigned>(endpoint));
            continue;
        }

        if (sClient->Subscribe(topic, qos, nullptr) != CHIP_NO_ERROR)
        {
            MATTER_MQTT_LOG_ERROR("command subscription subscribe failed topic=%s", topic);
            continue;
        }
        MATTER_MQTT_LOG("subscribed command subscription nodeId=0x" ChipLogFormatX64 " ep=%u topic=%s",
                        ChipLogValueX64(profile->GetMqttDeviceNodeId()), static_cast<unsigned>(endpoint), topic);
    }
}

void InitOnMatterThread(intptr_t /* arg */)
{
    if (sClient != nullptr && sClient->IsConnected())
    {
        SubscribeCommands();
    }
}

void FabricReadyOnMatterThread(intptr_t /* arg */)
{
    SubscribeCommands();
}

} // namespace

bool DispatchInbound(chip::CharSpan topic, chip::ByteSpan payload)
{
    if (sConfig.profile == nullptr || !sConfig.profile->IsMqttDeviceIdKnown() || topic.empty())
    {
        return false;
    }

    return OnInboundMessage(topic, payload);
}

CHIP_ERROR Init(chip::DeviceLayer::Silabs::MqttClient & client, const MatterMqttBridgeConfig & config)
{
    VerifyOrReturnError(config.profile != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(config.filterRegistry != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(config.reader != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(config.injector != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    MATTER_MQTT_LOG("Init (mqtt connected=%d)", client.IsConnected());

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    sClient = &client;
    sConfig = config;
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    ReturnErrorOnFailure(chip::DeviceLayer::PlatformMgr().ScheduleWork(InitOnMatterThread, 0));

    return CHIP_NO_ERROR;
}

void Shutdown()
{
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    sAttributePublishSeq = 0;
    for (size_t i = 0; i < kBridgeSlotCount; ++i)
    {
        sBridgeSlots[i].commandHeld.store(false, std::memory_order_relaxed);
        sBridgeSlots[i].inUse.store(false, std::memory_order_relaxed);
    }
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
}

void OnMatterFabricReady()
{
    LogErrorOnFailure(chip::DeviceLayer::PlatformMgr().ScheduleWork(FabricReadyOnMatterThread, 0));
}

void NotifyReportableAttributeChange(const chip::app::ConcreteAttributePath & path)
{
    DeviceMqttProfile * profile = sConfig.profile;
    VerifyOrReturn(profile != nullptr && profile->IsMqttDeviceIdKnown());
    VerifyOrReturn(profile->IsAttributeAllowed(path.mClusterId, path.mAttributeId));
    MATTER_MQTT_LOG_DETAIL("attribute change ep=%u attr=" ChipLogFormatMEI, static_cast<unsigned>(path.mEndpointId),
                           ChipLogValueMEI(path.mAttributeId));
    if (EnqueueReport(path) != CHIP_NO_ERROR)
    {
        MATTER_MQTT_LOG_ERROR("attribute change report dropped ep=%u attr=" ChipLogFormatMEI,
                              static_cast<unsigned>(path.mEndpointId), ChipLogValueMEI(path.mAttributeId));
    }
}

} // namespace MatterMqttBridge
