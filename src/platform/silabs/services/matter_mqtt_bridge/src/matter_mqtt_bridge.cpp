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

#include "matter_mqtt_attribute_listener.h"
#include "matter_mqtt_topic.h"
#include "matter_mqtt_types.h"
#include "matter_mqtt_value_converter.h"

#include <app/InteractionModelEngine.h>
#include <app/data-model-provider/Provider.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>
#include <system/SystemLayer.h>

#include <lib/support/Span.h>

#include <cstring>

namespace MatterMqttBridge {
namespace {

// Bridge runtime state; Init/Shutdown run from the app/MQTT callback thread; listener and invoke run on Matter thread.

constexpr bool kUseWildcardSubscribe = SL_MATTER_MQTT_BRIDGE_WILDCARD_SUBSCRIBE;

MatterMqttJsonCodec sCodec;
CodeDrivenAttributeReader sReader;
MqttCommandInjector sInjector;
MqttClientTransportPort sTransport;

MatterMqttBridgeConfig sConfig;
MatterMqttAttributeListener sListener;
bool sListenerRegistered = false;
bool sSubscribed         = false;
bool sSnapshotStarted    = false;
size_t sSnapshotIndex    = 0;
bool sSnapshotActive     = false;

char sCommandFilterTopic[MATTER_MQTT_MAX_TOPIC_LEN] = {};
char sOutboundTopic[MATTER_MQTT_MAX_TOPIC_LEN]      = {};
char sOutboundPayload[MATTER_MQTT_MAX_PAYLOAD_LEN]  = {};

struct InboundArgsScratch
{
    char names[MATTER_MQTT_MAX_ARGS][32];
    char values[MATTER_MQTT_MAX_ARGS][64];
    MatterMqttCodecTypes::ArgsView view{};
};

InboundArgsScratch sInboundArgs;
InboundCommand sScheduledInbound;

void Reject(const char * reason, const char * detail);

CHIP_ERROR EnqueueReport(const MatterMqttBridgeConfig & config, const chip::app::ConcreteAttributePath & path);
/** Matter thread: attribute read + queue publish work on the MQTT service thread. */
void PublishPath(const chip::app::ConcreteAttributePath & path);
void AdvanceSnapshot(intptr_t arg);
void ContinueSnapshot(intptr_t arg);
void StartSnapshot(intptr_t arg);
void SubscribeCommandsAndStartSnapshot();

void Reject(const char * reason, const char * detail)
{
    ChipLogError(AppServer, "MatterMqttBridge: rejected %s (%s)", reason, detail);
}

// Copy decoded JSON arg strings into static storage so they survive the cross-thread lambda to HandleInboundCommand.
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

// Flatten Matter path + scalar value into a POD copy for MqttClient::ScheduleWork (MQTT thread must not read attributes).
ReportWork MakeReportWork(const chip::app::ConcreteAttributePath & path, const RawValue & raw)
{
    ReportWork work;
    work.endpoint     = path.mEndpointId;
    work.clusterId    = path.mClusterId;
    work.attributeId  = path.mAttributeId;
    work.valueType    = raw.type;
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

/** Runs on the MQTT service thread: build attribute report topic + JSON payload, then Publish. */
void ReportWorkHandler(const uint8_t * data, size_t len)
{
    DeviceMqttProfile * profile = sConfig.profile;
    VerifyOrReturn(sConfig.transport != nullptr && sConfig.codec != nullptr && sConfig.adapters != nullptr && profile != nullptr);
    VerifyOrReturn(profile->IsMqttDeviceIdKnown());
    VerifyOrReturn(len == sizeof(ReportWork));

    const auto * work        = reinterpret_cast<const ReportWork *>(data);
    const ClusterAdapter * adapter = sConfig.adapters->FindByClusterId(work->clusterId);
    const MatterMqttAttributeEntry * attr = adapter != nullptr ? sConfig.adapters->FindAttribute(adapter, work->attributeId) : nullptr;
    char valueText[MATTER_MQTT_MAX_VALUE_LEN];
    char json[MATTER_MQTT_MAX_PAYLOAD_LEN];
    bool asNumber            = false;
    const char * attrName    = nullptr;

    VerifyOrReturn(adapter != nullptr);
    VerifyOrReturn(attr != nullptr);
    if (MatterMqttFormatReportValue(adapter, attr, RawFromWork(*work), valueText, sizeof(valueText), asNumber) != CHIP_NO_ERROR)
    {
        return;
    }

    if (sConfig.codec->EncodeReport(asNumber, chip::CharSpan::fromCharString(valueText), json, sizeof(json)) != CHIP_NO_ERROR)
    {
        return;
    }

    attrName = sConfig.adapters->AttributeNameOf(adapter, work->attributeId);
    VerifyOrReturn(attrName != nullptr);

    // matter/v1/{deviceId}/ep/{ep}/cluster/{name}/attr/{name}
    if (MatterMqttTopic::BuildAttributeTopic(chip::CharSpan::fromCharString(profile->GetMqttDeviceId()), work->endpoint,
                                             chip::CharSpan::fromCharString(adapter->clusterName),
                                             chip::CharSpan::fromCharString(attrName), sOutboundTopic,
                                             sizeof(sOutboundTopic)) != CHIP_NO_ERROR)
    {
        return;
    }

    strncpy(sOutboundPayload, json, sizeof(sOutboundPayload) - 1);
    sOutboundPayload[sizeof(sOutboundPayload) - 1] = '\0';
    const chip::ByteSpan payload(reinterpret_cast<uint8_t *>(sOutboundPayload), strlen(sOutboundPayload));
    const auto qos = static_cast<chip::DeviceLayer::Silabs::MqttQoS>(sConfig.reportQos);
    if (sConfig.transport->Publish(sOutboundTopic, payload, qos, sConfig.reportRetained) != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "MatterMqttBridge: Publish failed");
    }

    // Snapshot stepping reads attributes on the Matter thread; schedule there after each MQTT publish.
    if (sSnapshotActive)
    {
        chip::DeviceLayer::PlatformMgr().ScheduleWork(AdvanceSnapshot, 0);
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
    ReturnErrorOnFailure(config.reader->Read(path, raw));

    const ReportWork work = MakeReportWork(path, raw);
    ReturnErrorOnFailure(config.transport->ScheduleWork(ReportWorkHandler, &work, sizeof(work)));
    return CHIP_NO_ERROR;
}

void PublishPath(const chip::app::ConcreteAttributePath & path)
{
    if (EnqueueReport(sConfig, path) != CHIP_NO_ERROR && sSnapshotActive)
    {
        chip::DeviceLayer::PlatformMgr().ScheduleWork(AdvanceSnapshot, 0);
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

    const ClusterAdapter * adapter = sConfig.adapters->FindByClusterId(command.clusterId);
    VerifyOrReturn(adapter != nullptr);

    const MatterMqttCommandEntry * entry = sConfig.adapters->FindCommand(adapter, command.commandId);
    VerifyOrReturn(entry != nullptr);

    if (sConfig.injector->Invoke(command, *entry, &sInboundArgs.view) != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "MatterMqttBridge: command inject failed");
    }
}

// MQTT thread: parse command topic + JSON payload; may return false when topic is not for this device/command contract.
bool OnInboundMessage(chip::CharSpan topic, chip::ByteSpan payload)
{
    DeviceMqttProfile * profile = sConfig.profile;
    VerifyOrReturnError(sConfig.codec != nullptr && sConfig.adapters != nullptr && profile != nullptr, false);
    VerifyOrReturnError(profile->IsMqttDeviceIdKnown(), false);

    // --- Topic: extract endpoint, MQTT cluster name, command name; reject wrong device id ---
    MatterMqttTopic::ParsedCommandTopic parsed;
    if (MatterMqttTopic::ParseCommandTopic(topic, chip::CharSpan::fromCharString(profile->GetMqttDeviceId()), parsed) !=
        CHIP_NO_ERROR)
    {
        return false;
    }

    if (!profile->IsEndpointAllowed(parsed.endpoint))
    {
        Reject(parsed.commandName, "endpoint");
        return true;
    }

    // --- Map MQTT names to Matter cluster adapter + command id ---
    const ClusterAdapter * adapter = sConfig.adapters->FindByClusterName(chip::CharSpan::fromCharString(parsed.clusterName));
    if (adapter == nullptr)
    {
        Reject(parsed.commandName, "unknown_cluster");
        return true;
    }

    chip::CommandId commandId = 0;
    const MatterMqttCommandEntry * commandEntry = nullptr;
    if (!sConfig.adapters->ResolveCommandId(adapter, chip::CharSpan::fromCharString(parsed.commandName), commandId,
                                          &commandEntry) ||
        commandEntry == nullptr)
    {
        Reject(parsed.commandName, "unknown_command");
        return true;
    }

    // --- Product ACL (IsCommandAllowed) ---
    if (!profile->IsCommandAllowed(parsed.endpoint, adapter->clusterId, commandId))
    {
        Reject(parsed.commandName, "denied");
        return true;
    }

    // --- Adapter flags: no fabric-scoped, response, or explicitly unsupported commands ---
    if ((commandEntry->flags & kCommandFlagUnsupported) != 0 || (commandEntry->flags & kCommandFlagFabricScoped) != 0 ||
        (commandEntry->flags & kCommandFlagHasResponse) != 0)
    {
        Reject(parsed.commandName, "unsupported");
        return true;
    }

    // --- JSON payload -> ArgsView name/value pairs ---
    MatterMqttCodecTypes::ArgsView args;
    if (sConfig.codec->DecodeArgs(payload, args) != CHIP_NO_ERROR)
    {
        Reject(parsed.commandName, "args");
        return true;
    }

    // --- Pack numeric/bool args; schedule Matter-thread invoke ---
    InboundCommand inbound;
    inbound.endpoint  = parsed.endpoint;
    inbound.clusterId = adapter->clusterId;
    inbound.commandId = commandId;

    if (sConfig.adapters->PackCommandArgs(*commandEntry, args, inbound.args, inbound.argsLen) != CHIP_NO_ERROR)
    {
        Reject(parsed.commandName, "args_pack");
        return true;
    }

    StashInboundArgs(args);
    // Cross to Matter thread; InvokeCommand must not run on the MQTT service thread.
    sScheduledInbound = inbound;
    chip::DeviceLayer::SystemLayer().ScheduleLambda([]() { HandleInboundCommand(sScheduledInbound); });
    return true;
}

// Subscribe to matter/v1/{deviceId}/ep/+/cluster/+/cmd/[+] and kick off attribute snapshot.
void SubscribeCommandsAndStartSnapshot()
{
    DeviceMqttProfile * profile = sConfig.profile;
    VerifyOrReturn(sConfig.transport != nullptr && profile != nullptr);
    VerifyOrReturn(sConfig.transport->IsConnected());

    if (profile->RefreshMqttDeviceId() != CHIP_NO_ERROR || !profile->IsMqttDeviceIdKnown())
    {
        ChipLogProgress(AppServer, "MatterMqttBridge: MQTT connected; waiting for Matter fabric NodeId");
        return;
    }

    if (!sSubscribed)
    {
        if (MatterMqttTopic::BuildCommandFilter(chip::CharSpan::fromCharString(profile->GetMqttDeviceId()), kUseWildcardSubscribe,
                                                sCommandFilterTopic, sizeof(sCommandFilterTopic)) != CHIP_NO_ERROR)
        {
            ChipLogError(AppServer, "MatterMqttBridge: command filter build failed");
            return;
        }

        const auto qos = static_cast<chip::DeviceLayer::Silabs::MqttQoS>(sConfig.commandQos);
        if (sConfig.transport->Subscribe(sCommandFilterTopic, qos) != CHIP_NO_ERROR)
        {
            ChipLogError(AppServer, "MatterMqttBridge: Subscribe failed");
            return;
        }
        sSubscribed = true;
    }

    if (!sSnapshotStarted && profile->SupportsAttributePublishing() && profile->SnapshotPathCount() > 0)
    {
        sSnapshotStarted = true;
        chip::DeviceLayer::PlatformMgr().ScheduleWork(StartSnapshot, 0);
    }
}

void RegisterListener()
{
    // Attribute notifications from InteractionModelEngine::GetDataModelProvider().
    chip::app::DataModel::Provider * provider = chip::app::InteractionModelEngine::GetInstance()->GetDataModelProvider();
    VerifyOrReturn(provider != nullptr);

    if (!sListenerRegistered)
    {
        provider->RegisterAttributeChangeListener(sListener);
        sListenerRegistered = true;
    }
}

void UnregisterListener()
{
    chip::app::DataModel::Provider * provider = chip::app::InteractionModelEngine::GetInstance()->GetDataModelProvider();
    VerifyOrReturn(provider != nullptr);

    if (sListenerRegistered)
    {
        provider->UnregisterAttributeChangeListener(sListener);
        sListenerRegistered = false;
    }
}

void InitOnMatterThread(intptr_t /* arg */)
{
    if (sConfig.profile != nullptr && sConfig.profile->SupportsAttributePublishing())
    {
        sListener.Configure(sConfig, EnqueueReport);
        RegisterListener();
    }

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

    sTransport.Bind(client);
    sConfig           = config;
    sSubscribed       = false;
    sSnapshotStarted  = false;
    sSnapshotActive   = false;

    if (sConfig.transport == nullptr)
    {
        sConfig.transport = &sTransport;
    }
    if (sConfig.codec == nullptr)
    {
        sConfig.codec = &sCodec;
    }
    if (sConfig.reader == nullptr)
    {
        sConfig.reader = &sReader;
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
    sSubscribed      = false;
    sSnapshotStarted = false;
    sSnapshotActive  = false;

    chip::DeviceLayer::PlatformMgr().ScheduleWork(
        [](intptr_t) {
            UnregisterListener();
        },
        0);
}

#if SL_MATTER_MQTT_BRIDGE_POST_ATTRIBUTE_HOOK
void MatterMqttBridge::NotifyReportableAttributeChange(const chip::app::ConcreteAttributePath & path)
{
    DeviceMqttProfile * profile = sConfig.profile;
    VerifyOrReturn(profile != nullptr && profile->SupportsAttributePublishing());
    VerifyOrReturn(profile->IsEndpointAllowed(path.mEndpointId));
    VerifyOrReturn(profile->IsAttributeAllowed(path.mEndpointId, path.mClusterId, path.mAttributeId));
    (void) EnqueueReport(sConfig, path);
}
#endif

} // namespace MatterMqttBridge
