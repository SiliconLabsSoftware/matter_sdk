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

#include "MatterMqttContract.h"
#include "MatterMqttBridgeConfig.h"

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

/**
 * Wire the bridge to @p client and @p config. Command subscribe runs when the client
 * connects (@ref Shutdown on disconnect). @p config.profile must be non-null (product-defined).
 * Call from the MqttClient service thread (not the Matter thread). Uses LockChipStack/UnlockChipStack; not recursive.
 */
CHIP_ERROR Init(chip::DeviceLayer::Silabs::MqttClient & client, const MatterMqttBridgeConfig & config);

/**
 * Tear down MQTT subscribe state until the next Init.
 * Call from the MqttClient service thread. MqttClient drains queued publish callbacks before disconnect.
 * Slot release is idempotent.
 */
void Shutdown();

/**
 * Handle an inbound MQTT publish for the Matter command topic contract.
 * @return true if @p topic matched the device command filter and was consumed.
 * @p topic is not required to be null-terminated (MQTT RX lenstring).
 */
bool DispatchInbound(chip::CharSpan topic, chip::ByteSpan payload);

/** Ember post-write path (BaseApplication::MatterPostAttributeChangeCallback) -> EnqueueReport. */
void NotifyReportableAttributeChange(const chip::app::ConcreteAttributePath & path);

/** Re-run command subscribe when fabric NodeId becomes available (Matter thread). */
void OnMatterFabricReady();

} // namespace MatterMqttBridge