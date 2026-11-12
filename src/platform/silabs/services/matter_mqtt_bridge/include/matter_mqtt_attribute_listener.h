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

#include "matter_mqtt_bridge_config.h"

#include <app/data-model-provider/AttributeChangeListener.h>
#include <lib/core/CHIPError.h>

/**
 * Forwards non-quiet attribute changes allowed by DeviceMqttProfile to the bridge report pipeline.
 *
 * Primary path (IM + Ember via data model provider):
 * - RegisterAttributeChangeListener on InteractionModelEngine::GetDataModelProvider().
 * - Ember writes call emberAfAttributeChanged -> CodegenDataModelProvider::NotifyAttributeChanged -> listeners.
 * - Code-driven clusters call Provider::NotifyAttributeChanged / SetAttributeValue when implemented correctly.
 *
 * Gaps (no provider notification):
 * - emAfWriteAttribute with MarkAttributeDirty::kNo skips emberAfAttributeChanged but still runs MatterPostAttributeChangeCallback.
 * - Some legacy cluster paths emit only MatterPostAttributeChangeCallback.
 *
 * When SL_MATTER_MQTT_BRIDGE_POST_ATTRIBUTE_HOOK is set, BaseApplication forwards post-callback into
 * MatterMqttBridge::NotifyReportableAttributeChange, which uses the same EnqueueReport + attribute read
 * pipeline as this listener (value is still read via AttributeValueReader; post-callback bytes are not used yet).
 */
class MatterMqttAttributeListener : public chip::app::DataModel::AttributeChangeListener
{
public:
    using ReportHandler = CHIP_ERROR (*)(const MatterMqttBridgeConfig & config, const chip::app::ConcreteAttributePath & path);

    void Configure(const MatterMqttBridgeConfig & config, ReportHandler handler);

    void OnAttributeChanged(const chip::app::ConcreteAttributePath & path,
                            chip::app::DataModel::AttributeChangeType type) override;

private:
    const MatterMqttBridgeConfig * mConfig = nullptr;
    ReportHandler mHandler                 = nullptr;
};
