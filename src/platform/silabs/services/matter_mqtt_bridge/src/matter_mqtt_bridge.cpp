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

#include "matter_mqtt_bridge.h"

#include "matter_mqtt_log.h"
#include "matter_mqtt_topic.h"
#include "matter_mqtt_types.h"
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>
#include <system/SystemLayer.h>

#include <lib/support/Span.h>

#include <cinttypes>
#include <cstdio>
#include <cstring>

namespace MatterMqttBridge {
namespace {

// --- File-local types (bridge TU only; not published in headers) ---

/** Stable storage for JSON arg strings between MQTT-thread decode and Matter-thread invoke. */
struct InboundArgsScratch
{
    char names[MATTER_MQTT_MAX_ARGS][MATTER_MQTT_MAX_ARG_NAME_LEN];
    char values[MATTER_MQTT_MAX_ARGS][MATTER_MQTT_MAX_ARG_VALUE_LEN];
    MatterMqttCodecTypes::ArgsView view{};
};

/**
 * Shared bridge slot pool (depth matches MqttClient work queue): async attribute publish buffers on the MQTT thread,
 * or inbound command + args held until Matter-thread invoke completes.
 */
struct BridgeSlot
{
    bool inUse         = false;
    bool commandHeld   = false;
    char topic[MATTER_MQTT_MAX_TOPIC_LEN]{};
    char payload[MATTER_MQTT_MAX_PAYLOAD_LEN]{};
    size_t payloadLen  = 0;
    InboundCommand command;
    InboundArgsScratch args;
};

// --- Constants ---

constexpr size_t kBridgeSlotCount = 4;

// --- Static state (Init/Shutdown on app/MQTT paths; invoke on Matter thread) ---

MatterMqttJsonCodec sCodec;
MqttClientTransportPort sTransport;

MatterMqttBridgeConfig sConfig;
bool sSubscribed         = false;
bool sSnapshotStarted    = false;
size_t sSnapshotIndex    = 0;
bool sSnapshotActive     = false;

char sCommandFilterTopic[MATTER_MQTT_MAX_TOPIC_LEN] = {};

uint32_t sAttributePublishSeq = 0;
BridgeSlot sBridgeSlots[kBridgeSlotCount];

// --- Forward declarations ---

void Reject(chip::CommandId commandId, const char * reason);

BridgeSlot * AcquireBridgeSlot();
void ReleaseBridgeSlot(BridgeSlot * slot);
void OnAttributePublishDone(CHIP_ERROR result, void * context);

void StashInboundArgs(const MatterMqttCodecTypes::ArgsView & src, InboundArgsScratch & dest);

void ReportWorkHandler(const uint8_t * data, size_t len);

CHIP_ERROR EnqueueReport(const MatterMqttBridgeConfig & config, const chip::app::ConcreteAttributePath & path);
void PublishPath(const chip::app::ConcreteAttributePath & path);
void AdvanceSnapshot(intptr_t arg);
void ContinueSnapshot(intptr_t arg);
void StartSnapshot(intptr_t arg);

void HandleInboundCommandSlot(intptr_t arg);
bool OnInboundMessage(chip::CharSpan topic, chip::ByteSpan payload);

void SubscribeCommandsAndStartSnapshot();
void InitOnMatterThread(intptr_t arg);

// --- Inbound command reject logging ---

void Reject(chip::CommandId commandId, const char * reason)
{
    MATTER_MQTT_LOG_ERROR("command rejected cmd=" ChipLogFormatMEI " (%s)", ChipLogValueMEI(commandId), reason);
}

// --- Bridge slot pool (MQTT thread; publish and inbound command share depth) ---

BridgeSlot * AcquireBridgeSlot()
{
    for (size_t i = 0; i < kBridgeSlotCount; ++i)
    {
        if (!sBridgeSlots[i].inUse)
        {
            sBridgeSlots[i].inUse       = true;
            sBridgeSlots[i].commandHeld = false;
            sBridgeSlots[i].payloadLen  = 0;
            return &sBridgeSlots[i];
        }
    }
    return nullptr;
}

void ReleaseBridgeSlot(BridgeSlot * slot)
{
    if (slot != nullptr)
    {
        slot->inUse       = false;
        slot->commandHeld = false;
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

// --- Cross-thread command arg stash ---

void StashInboundArgs(const MatterMqttCodecTypes::ArgsView & src, InboundArgsScratch & dest)
{
    dest.view.count = 0;
    for (size_t i = 0; i < src.count && i < MATTER_MQTT_MAX_ARGS; ++i)
    {
        strncpy(dest.names[i], src.items[i].name, sizeof(dest.names[i]) - 1);
        strncpy(dest.values[i], src.items[i].value, sizeof(dest.values[i]) - 1);
        dest.view.items[i].name  = dest.names[i];
        dest.view.items[i].value = dest.values[i];
        ++dest.view.count;
    }
}

/** Runs on the MQTT service thread: build attribute report topic + JSON payload, then Publish. */
void ReportWorkHandler(const uint8_t * data, size_t len)
{
    DeviceMqttProfile * profile = sConfig.profile;
    VerifyOrReturn(sConfig.transport != nullptr && sConfig.filterRegistry != nullptr && profile != nullptr);
    VerifyOrReturn(profile->IsMqttDeviceIdKnown());
    VerifyOrReturn(len == sizeof(ReportWork));

    const auto * work               = reinterpret_cast<const ReportWork *>(data);
    const MatterMqttClusterFilter * clusterFilter = sConfig.filterRegistry->FindByClusterId(work->clusterId);
    const MatterMqttFilterAttributeEntry * attr =
        sConfig.filterRegistry->FindAttribute(clusterFilter, work->attributeId);
    VerifyOrReturn(clusterFilter != nullptr && attr != nullptr);
    VerifyOrReturn(work->value[0] != '\0');

    char json[MATTER_MQTT_MAX_PAYLOAD_LEN];

    VerifyOrReturn(sCodec.EncodeReport(chip::CharSpan::fromCharString(work->value), json, sizeof(json)) == CHIP_NO_ERROR);

    BridgeSlot * slot = AcquireBridgeSlot();
    if (slot == nullptr)
    {
        MATTER_MQTT_LOG_ERROR("attribute report skipped: bridge slot pool full");
        if (sSnapshotActive)
        {
            LogErrorOnFailure(chip::DeviceLayer::PlatformMgr().ScheduleWork(AdvanceSnapshot, 0));
        }
        return;
    }

    if (MatterMqttTopic::BuildAttributeTopic(profile->GetMqttDeviceNodeId(), work->endpoint, work->clusterId,
                                             work->attributeId, slot->topic, sizeof(slot->topic)) != CHIP_NO_ERROR)
    {
        ReleaseBridgeSlot(slot);
        return;
    }

    strncpy(slot->payload, json, sizeof(slot->payload) - 1);
    slot->payload[sizeof(slot->payload) - 1] = '\0';
    slot->payloadLen                         = strlen(slot->payload);
    const chip::ByteSpan payload(reinterpret_cast<uint8_t *>(slot->payload), slot->payloadLen);
    const auto qos            = static_cast<chip::DeviceLayer::Silabs::MqttQoS>(sConfig.reportQos);
    const uint32_t publishSeq = ++sAttributePublishSeq;
    MATTER_MQTT_LOG("attribute publish #%" PRIu32 " topic=%s payload=%s", publishSeq, slot->topic, slot->payload);

    const CHIP_ERROR publishErr =
        sConfig.transport->Publish(slot->topic, payload, qos, sConfig.reportRetained, OnAttributePublishDone, slot);
    if (publishErr != CHIP_NO_ERROR)
    {
        MATTER_MQTT_LOG_ERROR("attribute publish #%" PRIu32 " queue failed topic=%s", publishSeq, slot->topic);
        ReleaseBridgeSlot(slot);
    }

    // Snapshot stepping reads attributes on the Matter thread; schedule there after each MQTT publish.
    if (sSnapshotActive)
    {
        LogErrorOnFailure(chip::DeviceLayer::PlatformMgr().ScheduleWork(AdvanceSnapshot, 0));
    }
}

// Matter thread: read attribute, then hand a ReportWork blob to the MQTT service thread.
CHIP_ERROR EnqueueReport(const MatterMqttBridgeConfig & config, const chip::app::ConcreteAttributePath & path)
{
    VerifyOrReturnError(config.transport != nullptr && config.transport->IsConnected(), CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(config.reader != nullptr && config.filterRegistry != nullptr, CHIP_ERROR_INCORRECT_STATE);

    const MatterMqttClusterFilter * clusterFilter = config.filterRegistry->FindByClusterId(path.mClusterId);
    VerifyOrReturnError(clusterFilter != nullptr, CHIP_ERROR_NOT_FOUND);
    VerifyOrReturnError(config.filterRegistry->FindAttribute(clusterFilter, path.mAttributeId) != nullptr, CHIP_ERROR_NOT_FOUND);

    ReportWork work{};
    work.endpoint    = path.mEndpointId;
    work.clusterId   = path.mClusterId;
    work.attributeId = path.mAttributeId;

    const CHIP_ERROR readErr = config.reader->ReadAsString(path, work.value, sizeof(work.value));
    if (readErr != CHIP_NO_ERROR)
    {
        MATTER_MQTT_LOG_ERROR("attribute read failed ep=%u err=%" CHIP_ERROR_FORMAT, static_cast<unsigned>(path.mEndpointId),
                              readErr.Format());
        return readErr;
    }

    const CHIP_ERROR schedErr = config.transport->ScheduleWork(ReportWorkHandler, &work, sizeof(work));
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

void PublishPath(const chip::app::ConcreteAttributePath & path)
{
    if (EnqueueReport(sConfig, path) != CHIP_NO_ERROR && sSnapshotActive)
    {
        LogErrorOnFailure(chip::DeviceLayer::PlatformMgr().ScheduleWork(AdvanceSnapshot, 0));
    }
}

void AdvanceSnapshot(intptr_t /* arg */)
{
    ++sSnapshotIndex;
    if (sConfig.profile == nullptr || sSnapshotIndex >= sConfig.profile->SnapshotPathCount())
    {
        sSnapshotActive = false;
        return;
    }
    ContinueSnapshot(0);
}

void ContinueSnapshot(intptr_t /* arg */)
{
    VerifyOrReturn(sSnapshotActive && sConfig.profile != nullptr);
    const MatterMqttSnapshotPath * paths = sConfig.profile->SnapshotPaths();
    VerifyOrReturn(paths != nullptr);
    VerifyOrReturn(sSnapshotIndex < sConfig.profile->SnapshotPathCount());

    const MatterMqttSnapshotPath & item = paths[sSnapshotIndex];
    PublishPath(chip::app::ConcreteAttributePath(item.endpoint, item.clusterId, item.attributeId));
}

void StartSnapshot(intptr_t /* arg */)
{
    sSnapshotIndex  = 0;
    sSnapshotActive = true;
    ContinueSnapshot(0);
}

// Matter thread: TLV invoke from slot-owned JSON command args.
void HandleInboundCommandSlot(intptr_t arg)
{
    const size_t index = static_cast<size_t>(arg);
    VerifyOrReturn(index < kBridgeSlotCount);

    BridgeSlot * slot = &sBridgeSlots[index];
    VerifyOrReturn(slot->inUse && slot->commandHeld);
    VerifyOrReturn(sConfig.injector != nullptr && sConfig.filterRegistry != nullptr);

    const InboundCommand & command = slot->command;
    MATTER_MQTT_LOG("inbound command ep=%u cluster=" ChipLogFormatMEI " cmd=" ChipLogFormatMEI,
                    static_cast<unsigned>(command.endpoint), ChipLogValueMEI(command.clusterId),
                    ChipLogValueMEI(command.commandId));

    const MatterMqttClusterFilter * clusterFilter = sConfig.filterRegistry->FindByClusterId(command.clusterId);
    if (clusterFilter == nullptr)
    {
        ReleaseBridgeSlot(slot);
        return;
    }

    const MatterMqttFilterCommandEntry * entry = sConfig.filterRegistry->FindCommand(clusterFilter, command.commandId);
    if (entry == nullptr)
    {
        ReleaseBridgeSlot(slot);
        return;
    }

    if (sConfig.injector->Invoke(command, *entry, &slot->args.view) != CHIP_NO_ERROR)
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

    // --- Topic: extract endpoint, cluster id, command id; reject wrong device id ---
    MatterMqttTopic::ParsedCommandTopic parsed;
    if (MatterMqttTopic::ParseCommandTopic(topic, profile->GetMqttDeviceNodeId(), parsed) != CHIP_NO_ERROR)
    {
        return false;
    }

    const chip::CommandId commandId = parsed.commandId;

    if (!profile->IsEndpointAllowed(parsed.endpoint))
    {
        Reject(commandId, "endpoint");
        return true;
    }

    const MatterMqttClusterFilter * clusterFilter = sConfig.filterRegistry->FindByClusterId(parsed.clusterId);
    if (clusterFilter == nullptr)
    {
        Reject(commandId, "unknown_cluster");
        return true;
    }

    const MatterMqttFilterCommandEntry * commandEntry = sConfig.filterRegistry->FindCommand(clusterFilter, commandId);
    if (commandEntry == nullptr)
    {
        Reject(commandId, "unknown_command");
        return true;
    }

    if (!profile->IsCommandAllowed(parsed.endpoint, clusterFilter->clusterId, commandId))
    {
        Reject(commandId, "denied");
        return true;
    }

    // --- Adapter flags: no fabric-scoped, response, or explicitly unsupported commands ---
    if ((commandEntry->flags & kCommandFlagUnsupported) != 0 || (commandEntry->flags & kCommandFlagFabricScoped) != 0 ||
        (commandEntry->flags & kCommandFlagHasResponse) != 0)
    {
        Reject(commandId, "unsupported");
        return true;
    }

    // --- JSON payload -> ArgsView name/value pairs ---
    MatterMqttCodecTypes::ArgsView args;
    if (sCodec.DecodeArgs(payload, args) != CHIP_NO_ERROR)
    {
        Reject(commandId, "args");
        return true;
    }

    BridgeSlot * slot = AcquireBridgeSlot();
    if (slot == nullptr)
    {
        Reject(commandId, "busy");
        return true;
    }

    StashInboundArgs(args, slot->args);

    if (sConfig.injector->ValidateArgs(clusterFilter->clusterId, commandId, &slot->args.view) != CHIP_NO_ERROR)
    {
        ReleaseBridgeSlot(slot);
        Reject(commandId, "args_pack");
        return true;
    }

    slot->commandHeld         = true;
    slot->command.endpoint    = parsed.endpoint;
    slot->command.clusterId   = clusterFilter->clusterId;
    slot->command.commandId   = commandId;

    // Cross to Matter thread on CHIP stack; InvokeCommand must not run on the MQTT service thread.
    const size_t slotIndex = static_cast<size_t>(slot - sBridgeSlots);
    if (chip::DeviceLayer::PlatformMgr().ScheduleWork(HandleInboundCommandSlot, slotIndex) != CHIP_NO_ERROR)
    {
        ReleaseBridgeSlot(slot);
        Reject(commandId, "schedule");
        return true;
    }
    return true;
}

// Subscribe to matter/v1/{deviceId}/ep/+/cluster/+/cmd/+ and kick off attribute snapshot.
void SubscribeCommandsAndStartSnapshot()
{
    DeviceMqttProfile * profile = sConfig.profile;
    VerifyOrReturn(sConfig.transport != nullptr && profile != nullptr);
    VerifyOrReturn(sConfig.transport->IsConnected());

    if (profile->RefreshMqttDeviceId() != CHIP_NO_ERROR || !profile->IsMqttDeviceIdKnown())
    {
        MATTER_MQTT_LOG("MQTT connected; waiting for Matter fabric NodeId");
        return;
    }

    if (!sSubscribed)
    {
        if (MatterMqttTopic::BuildCommandFilter(profile->GetMqttDeviceNodeId(), sCommandFilterTopic,
                                                sizeof(sCommandFilterTopic)) != CHIP_NO_ERROR)
        {
            MATTER_MQTT_LOG_ERROR("command filter topic build failed");
            return;
        }

        const auto qos = static_cast<chip::DeviceLayer::Silabs::MqttQoS>(sConfig.commandQos);
        if (sConfig.transport->Subscribe(sCommandFilterTopic, qos) != CHIP_NO_ERROR)
        {
            MATTER_MQTT_LOG_ERROR("command filter subscribe failed topic=%s", sCommandFilterTopic);
            return;
        }
        sSubscribed = true;
        MATTER_MQTT_LOG("subscribed command filter deviceId=%s topic=%s", profile->GetMqttDeviceId(), sCommandFilterTopic);
    }

    if (!sSnapshotStarted && profile->SupportsAttributePublishing() && profile->SnapshotPathCount() > 0)
    {
        sSnapshotStarted = true;
        MATTER_MQTT_LOG("starting attribute snapshot (%u paths)", static_cast<unsigned>(profile->SnapshotPathCount()));
        LogErrorOnFailure(chip::DeviceLayer::PlatformMgr().ScheduleWork(StartSnapshot, 0));
    }
}

void InitOnMatterThread(intptr_t /* arg */)
{
    if (sConfig.transport != nullptr && sConfig.transport->IsConnected())
    {
        SubscribeCommandsAndStartSnapshot();
    }
}

} // namespace

bool MatterMqttBridge::DispatchInbound(chip::CharSpan topic, chip::ByteSpan payload)
{
    // Ignore stray publishes until command subscription is active (avoids parsing during bootstrap).
    if (!sSubscribed || sConfig.profile == nullptr || !sConfig.profile->IsMqttDeviceIdKnown() || topic.empty())
    {
        return false;
    }

    return OnInboundMessage(topic, payload);
}

CHIP_ERROR MatterMqttBridge::Init(chip::DeviceLayer::Silabs::MqttClient & client, const MatterMqttBridgeConfig & config)
{
    VerifyOrReturnError(config.profile != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(config.filterRegistry != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(config.reader != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(config.injector != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    MATTER_MQTT_LOG("Init (mqtt connected=%d)", client.IsConnected());

    sTransport.Bind(client);
    sConfig          = config;
    sSubscribed      = false;
    sSnapshotStarted = false;
    sSnapshotActive  = false;

    if (sConfig.transport == nullptr)
    {
        sConfig.transport = &sTransport;
    }
    ReturnErrorOnFailure(chip::DeviceLayer::PlatformMgr().ScheduleWork(InitOnMatterThread, 0));

    return CHIP_NO_ERROR;
}

void MatterMqttBridge::Shutdown()
{
    sSubscribed            = false;
    sSnapshotStarted       = false;
    sSnapshotActive        = false;
    sAttributePublishSeq = 0;
    for (size_t i = 0; i < kBridgeSlotCount; ++i)
    {
        sBridgeSlots[i].inUse       = false;
        sBridgeSlots[i].commandHeld = false;
    }
}

void MatterMqttBridge::NotifyReportableAttributeChange(const chip::app::ConcreteAttributePath & path)
{
    DeviceMqttProfile * profile = sConfig.profile;
    VerifyOrReturn(profile != nullptr && profile->SupportsAttributePublishing());
    VerifyOrReturn(profile->IsEndpointAllowed(path.mEndpointId));
    VerifyOrReturn(profile->IsAttributeAllowed(path.mEndpointId, path.mClusterId, path.mAttributeId));
    MATTER_MQTT_LOG_DETAIL("attribute change ep=%u attr=" ChipLogFormatMEI, static_cast<unsigned>(path.mEndpointId),
                           ChipLogValueMEI(path.mAttributeId));
    (void) EnqueueReport(sConfig, path);
}

} // namespace MatterMqttBridge
