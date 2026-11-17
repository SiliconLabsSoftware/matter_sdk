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

/** Owned MQTT packet buffers for async Publish (pointers valid until publish callback). */
struct AttributePublishSlot
{
    bool inUse        = false;
    char topic[MATTER_MQTT_MAX_TOPIC_LEN]{};
    char payload[MATTER_MQTT_MAX_PAYLOAD_LEN]{};
    size_t payloadLen = 0;
};

// --- Constants ---

// Match MqttClient queue depth so burst attribute reports can each hold a stable topic/payload.
constexpr size_t kAttributePublishSlotCount = 4;

// --- Static state (Init/Shutdown on app/MQTT paths; invoke on Matter thread) ---

MatterMqttJsonCodec sCodec;
MqttCommandInjector sInjector;
MqttClientTransportPort sTransport;

MatterMqttBridgeConfig sConfig;
bool sSubscribed         = false;
bool sSnapshotStarted    = false;
size_t sSnapshotIndex    = 0;
bool sSnapshotActive     = false;

char sCommandFilterTopic[MATTER_MQTT_MAX_TOPIC_LEN] = {};

InboundArgsScratch sInboundArgs;
InboundCommand sScheduledInbound;

uint32_t sAttributePublishSeq = 0;
AttributePublishSlot sAttributePublishSlots[kAttributePublishSlotCount];

// --- Forward declarations ---

void Reject(chip::CommandId commandId, const char * reason);

AttributePublishSlot * AcquireAttributePublishSlot();
void ReleaseAttributePublishSlot(AttributePublishSlot * slot);
void OnAttributePublishDone(CHIP_ERROR result, void * context);

void StashInboundArgs(const MatterMqttCodecTypes::ArgsView & src);
ReportWork MakeReportWork(const chip::app::ConcreteAttributePath & path, const RawValue & raw);
RawValue RawFromWork(const ReportWork & work);

CHIP_ERROR MatterMqttFormatReportValue(const ClusterAdapter * adapter, const MatterMqttAttributeEntry * attr,
                                       const RawValue & raw, char * out, size_t outLen, bool & asNumber);

void ReportWorkHandler(const uint8_t * data, size_t len);

CHIP_ERROR EnqueueReport(const MatterMqttBridgeConfig & config, const chip::app::ConcreteAttributePath & path);
void PublishPath(const chip::app::ConcreteAttributePath & path);
void AdvanceSnapshot(intptr_t arg);
void ContinueSnapshot(intptr_t arg);
void StartSnapshot(intptr_t arg);

void HandleInboundCommand(InboundCommand command);
bool OnInboundMessage(chip::CharSpan topic, chip::ByteSpan payload);

void SubscribeCommandsAndStartSnapshot();
void InitOnMatterThread(intptr_t arg);

// --- Inbound command reject logging ---

void Reject(chip::CommandId commandId, const char * reason)
{
    MATTER_MQTT_LOG_ERROR("command rejected cmd=" ChipLogFormatMEI " (%s)", ChipLogValueMEI(commandId), reason);
}

// --- Publish slot pool (MQTT thread) ---

AttributePublishSlot * AcquireAttributePublishSlot()
{
    for (size_t i = 0; i < kAttributePublishSlotCount; ++i)
    {
        if (!sAttributePublishSlots[i].inUse)
        {
            sAttributePublishSlots[i].inUse      = true;
            sAttributePublishSlots[i].payloadLen = 0;
            return &sAttributePublishSlots[i];
        }
    }
    return nullptr;
}

void ReleaseAttributePublishSlot(AttributePublishSlot * slot)
{
    if (slot != nullptr)
    {
        slot->inUse = false;
    }
}

void OnAttributePublishDone(CHIP_ERROR result, void * context)
{
    ReleaseAttributePublishSlot(static_cast<AttributePublishSlot *>(context));
    if (result != CHIP_NO_ERROR)
    {
        MATTER_MQTT_LOG_ERROR("attribute publish async failed err=%" CHIP_ERROR_FORMAT, result.Format());
    }
}

// --- Cross-thread command arg stash ---

void StashInboundArgs(const MatterMqttCodecTypes::ArgsView & src)
{
    sInboundArgs.view.count = 0;
    for (size_t i = 0; i < src.count && i < MATTER_MQTT_MAX_ARGS; ++i)
    {
        strncpy(sInboundArgs.names[i], src.items[i].name, sizeof(sInboundArgs.names[i]) - 1);
        strncpy(sInboundArgs.values[i], src.items[i].value, sizeof(sInboundArgs.values[i]) - 1);
        sInboundArgs.view.items[i].name  = sInboundArgs.names[i];
        sInboundArgs.view.items[i].value = sInboundArgs.values[i];
        ++sInboundArgs.view.count;
    }
}

// --- ReportWork helpers ---

ReportWork MakeReportWork(const chip::app::ConcreteAttributePath & path, const RawValue & raw)
{
    ReportWork work;
    work.endpoint    = path.mEndpointId;
    work.clusterId   = path.mClusterId;
    work.attributeId = path.mAttributeId;
    work.valueType   = raw.type;
    switch (raw.type)
    {
    case RawValueType::kBool:
        work.value.boolean = raw.data.boolean ? 1 : 0;
        break;
    case RawValueType::kSigned:
        work.value.signedValue = raw.data.signedValue;
        break;
    case RawValueType::kUnsigned:
        work.value.unsignedValue = raw.data.unsignedValue;
        break;
    default:
        break;
    }
    return work;
}

RawValue RawFromWork(const ReportWork & work)
{
    RawValue raw;
    raw.type = work.valueType;
    switch (work.valueType)
    {
    case RawValueType::kBool:
        raw.data.boolean = work.value.boolean != 0;
        break;
    case RawValueType::kSigned:
        raw.data.signedValue = work.value.signedValue;
        break;
    case RawValueType::kUnsigned:
        raw.data.unsignedValue = work.value.unsignedValue;
        break;
    default:
        break;
    }
    return raw;
}

CHIP_ERROR MatterMqttFormatReportValue(const ClusterAdapter * adapter, const MatterMqttAttributeEntry * attr,
                                       const RawValue & raw, char * out, size_t outLen, bool & asNumber)
{
    VerifyOrReturnError(adapter != nullptr && attr != nullptr && out != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    // asNumber true -> EncodeReport emits unquoted JSON number; false -> quoted string or enum name.
    asNumber = false;

    if (raw.type == RawValueType::kNull)
    {
        strncpy(out, "null", outLen - 1);
        out[outLen - 1] = '\0';
        return CHIP_NO_ERROR;
    }

    if (attr->quantity == MatterMqttAttributeQuantity::kScalar && raw.type == RawValueType::kBool)
    {
        const char * text = raw.data.boolean ? "on" : "off";
        strncpy(out, text, outLen - 1);
        out[outLen - 1] = '\0';
        return CHIP_NO_ERROR;
    }

    if (attr->quantity == MatterMqttAttributeQuantity::kEnum && adapter->formatEnum != nullptr &&
        raw.type == RawValueType::kUnsigned)
    {
        const char * name = adapter->formatEnum(attr->id, raw.data.unsignedValue);
        VerifyOrReturnError(name != nullptr, CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE);
        strncpy(out, name, outLen - 1);
        out[outLen - 1] = '\0';
        return CHIP_NO_ERROR;
    }

    if (raw.type == RawValueType::kSigned || raw.type == RawValueType::kUnsigned)
    {
        const int32_t value =
            raw.type == RawValueType::kSigned ? raw.data.signedValue : static_cast<int32_t>(raw.data.unsignedValue);
        const int n = snprintf(out, outLen, "%d", static_cast<int>(value));
        VerifyOrReturnError(n >= 0 && static_cast<size_t>(n) < outLen, CHIP_ERROR_BUFFER_TOO_SMALL);
        asNumber = true;
        return CHIP_NO_ERROR;
    }

    if (raw.type == RawValueType::kBool)
    {
        strncpy(out, raw.data.boolean ? "true" : "false", outLen - 1);
        out[outLen - 1] = '\0';
        return CHIP_NO_ERROR;
    }

    return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
}

/** Runs on the MQTT service thread: build attribute report topic + JSON payload, then Publish. */
void ReportWorkHandler(const uint8_t * data, size_t len)
{
    DeviceMqttProfile * profile = sConfig.profile;
    VerifyOrReturn(sConfig.transport != nullptr && sConfig.codec != nullptr && sConfig.adapters != nullptr && profile != nullptr);
    VerifyOrReturn(profile->IsMqttDeviceIdKnown());
    VerifyOrReturn(len == sizeof(ReportWork));

    const auto * work              = reinterpret_cast<const ReportWork *>(data);
    const ClusterAdapter * adapter = sConfig.adapters->FindByClusterId(work->clusterId);
    const MatterMqttAttributeEntry * attr = sConfig.adapters->FindAttribute(adapter, work->attributeId);
    VerifyOrReturn(adapter != nullptr && attr != nullptr);

    char valueText[MATTER_MQTT_MAX_VALUE_LEN];
    char json[MATTER_MQTT_MAX_PAYLOAD_LEN];
    bool asNumber = false;

    VerifyOrReturn(MatterMqttFormatReportValue(adapter, attr, RawFromWork(*work), valueText, sizeof(valueText), asNumber) ==
                   CHIP_NO_ERROR);
    VerifyOrReturn(sConfig.codec->EncodeReport(asNumber, chip::CharSpan::fromCharString(valueText), json, sizeof(json)) ==
                   CHIP_NO_ERROR);

    AttributePublishSlot * slot = AcquireAttributePublishSlot();
    if (slot == nullptr)
    {
        MATTER_MQTT_LOG_ERROR("attribute report skipped: publish slot pool full");
        if (sSnapshotActive)
        {
            LogErrorOnFailure(chip::DeviceLayer::PlatformMgr().ScheduleWork(AdvanceSnapshot, 0));
        }
        return;
    }

    if (MatterMqttTopic::BuildAttributeTopic(profile->GetMqttDeviceNodeId(), work->endpoint, work->clusterId,
                                             work->attributeId, slot->topic, sizeof(slot->topic)) != CHIP_NO_ERROR)
    {
        ReleaseAttributePublishSlot(slot);
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
        ReleaseAttributePublishSlot(slot);
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
    VerifyOrReturnError(config.reader != nullptr && config.adapters != nullptr, CHIP_ERROR_INCORRECT_STATE);

    // Skip clusters/attributes not in the product adapter table (no MQTT mapping).
    const ClusterAdapter * adapter = config.adapters->FindByClusterId(path.mClusterId);
    VerifyOrReturnError(adapter != nullptr, CHIP_ERROR_NOT_FOUND);
    VerifyOrReturnError(config.adapters->FindAttribute(adapter, path.mAttributeId) != nullptr, CHIP_ERROR_NOT_FOUND);

    // Read on Matter thread; publish work runs on MQTT thread.
    RawValue raw;
    const CHIP_ERROR readErr = config.reader->Read(path, raw);
    if (readErr != CHIP_NO_ERROR)
    {
        MATTER_MQTT_LOG_ERROR("attribute read failed ep=%u err=%" CHIP_ERROR_FORMAT, static_cast<unsigned>(path.mEndpointId),
                              readErr.Format());
        return readErr;
    }

    const ReportWork work     = MakeReportWork(path, raw);
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

// Matter thread: TLV invoke from packed command args (decoded JSON already applied in PackCommandArgs).
void HandleInboundCommand(InboundCommand command)
{
    VerifyOrReturn(sConfig.injector != nullptr && sConfig.adapters != nullptr);

    MATTER_MQTT_LOG("inbound command ep=%u cluster=" ChipLogFormatMEI " cmd=" ChipLogFormatMEI,
                    static_cast<unsigned>(command.endpoint), ChipLogValueMEI(command.clusterId),
                    ChipLogValueMEI(command.commandId));

    const ClusterAdapter * adapter = sConfig.adapters->FindByClusterId(command.clusterId);
    VerifyOrReturn(adapter != nullptr);

    const MatterMqttCommandEntry * entry = sConfig.adapters->FindCommand(adapter, command.commandId);
    VerifyOrReturn(entry != nullptr);

    if (sConfig.injector->Invoke(command, *entry, &sInboundArgs.view) != CHIP_NO_ERROR)
    {
        MATTER_MQTT_LOG_ERROR("command inject failed");
    }
}

// MQTT thread: parse command topic + JSON payload; may return false when topic is not for this device/command contract.
bool OnInboundMessage(chip::CharSpan topic, chip::ByteSpan payload)
{
    DeviceMqttProfile * profile = sConfig.profile;
    VerifyOrReturnError(sConfig.codec != nullptr && sConfig.adapters != nullptr && profile != nullptr, false);
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

    // --- Map topic ids to Matter cluster adapter + command entry ---
    const ClusterAdapter * adapter = sConfig.adapters->FindByClusterId(parsed.clusterId);
    if (adapter == nullptr)
    {
        Reject(commandId, "unknown_cluster");
        return true;
    }

    const MatterMqttCommandEntry * commandEntry = sConfig.adapters->FindCommand(adapter, commandId);
    if (commandEntry == nullptr)
    {
        Reject(commandId, "unknown_command");
        return true;
    }

    // --- Product ACL (IsCommandAllowed) ---
    if (!profile->IsCommandAllowed(parsed.endpoint, adapter->clusterId, commandId))
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
    if (sConfig.codec->DecodeArgs(payload, args) != CHIP_NO_ERROR)
    {
        Reject(commandId, "args");
        return true;
    }

    // --- Pack numeric/bool args; schedule Matter-thread invoke ---
    InboundCommand inbound;
    inbound.endpoint  = parsed.endpoint;
    inbound.clusterId = adapter->clusterId;
    inbound.commandId = commandId;

    if (sConfig.adapters->PackCommandArgs(*commandEntry, args, inbound.args, inbound.argsLen) != CHIP_NO_ERROR)
    {
        Reject(commandId, "args_pack");
        return true;
    }

    StashInboundArgs(args);
    // Cross to Matter thread; InvokeCommand must not run on the MQTT service thread.
    sScheduledInbound = inbound;
    chip::DeviceLayer::SystemLayer().ScheduleLambda([]() { HandleInboundCommand(sScheduledInbound); });
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
    VerifyOrReturnError(config.adapters != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(config.reader != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

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
    if (sConfig.codec == nullptr)
    {
        sConfig.codec = &sCodec;
    }
    if (sConfig.injector == nullptr)
    {
        sConfig.injector = &sInjector;
    }

    ReturnErrorOnFailure(chip::DeviceLayer::PlatformMgr().ScheduleWork(InitOnMatterThread, 0));

    // Covers MQTT connect completing on the app thread before InitOnMatterThread runs (subscribe is idempotent).
    if (sTransport.IsConnected())
    {
        SubscribeCommandsAndStartSnapshot();
    }

    return CHIP_NO_ERROR;
}

void MatterMqttBridge::Shutdown()
{
    sSubscribed            = false;
    sSnapshotStarted       = false;
    sSnapshotActive        = false;
    sAttributePublishSeq = 0;
    for (size_t i = 0; i < kAttributePublishSlotCount; ++i)
    {
        sAttributePublishSlots[i].inUse = false;
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