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

#pragma once

#include "matter_mqtt_contract.h"
#include "matter_mqtt_bridge_config.h"

#include <app/ConcreteAttributePath.h>

#include <lib/core/CHIPError.h>
#include <lib/support/Span.h>
#include <platform/silabs/services/mqtt_client.h>

/**
 * Orchestrates Matter attribute reports and command injection over MQTT (contract v1).
 * Threading: attribute reads and command invoke on the Matter platform thread; publish/subscribe work on
 * the MqttClient service thread.
 */
namespace MatterMqttBridge {

class MatterMqttBridge
{
public:
    /**
     * Wire the bridge to @p client and @p config. MQTT subscribe/publish and snapshot run when the client
     * connects (@ref Shutdown on disconnect). @p config.profile must be non-null (product-defined).
     */
    static CHIP_ERROR Init(chip::DeviceLayer::Silabs::MqttClient & client, const MatterMqttBridgeConfig & config);

    /** Tear down attribute listener and MQTT subscribe state until the next Init. */
    static void Shutdown();

    /**
     * Handle an inbound MQTT publish for the Matter command topic contract.
     * @return true if @p topic matched the device command filter and was consumed.
     * @p topic is not required to be null-terminated (MQTT RX lenstring).
     */
    static bool DispatchInbound(chip::CharSpan topic, chip::ByteSpan payload);

#if SL_MATTER_MQTT_BRIDGE_POST_ATTRIBUTE_HOOK
    /**
     * Optional hook from MatterPostAttributeChangeCallback when provider listeners did not run
     * (e.g. MarkAttributeDirty::kNo). Uses the same EnqueueReport path as the attribute listener.
     */
    static void NotifyReportableAttributeChange(const chip::app::ConcreteAttributePath & path);
#endif
};

// Public entry points: types live in namespace MatterMqttBridge; orchestration is class MatterMqttBridge.
inline CHIP_ERROR Init(chip::DeviceLayer::Silabs::MqttClient & client, const MatterMqttBridgeConfig & config)
{
    return ::MatterMqttBridge::MatterMqttBridge::Init(client, config);
}

inline void Shutdown()
{
    ::MatterMqttBridge::MatterMqttBridge::Shutdown();
}

inline bool DispatchInbound(chip::CharSpan topic, chip::ByteSpan payload)
{
    return ::MatterMqttBridge::MatterMqttBridge::DispatchInbound(topic, payload);
}

#if SL_MATTER_MQTT_BRIDGE_POST_ATTRIBUTE_HOOK
inline void NotifyReportableAttributeChange(const chip::app::ConcreteAttributePath & path)
{
    ::MatterMqttBridge::MatterMqttBridge::NotifyReportableAttributeChange(path);
}
#endif

} // namespace MatterMqttBridge
