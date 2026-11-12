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

#if defined(ZCL_USING_ON_OFF_CLUSTER_SERVER)

// TODO(mqtt-bridge): Thermostat app currently omits OnOff from ZAP; enable ZCL_USING_ON_OFF_CLUSTER_SERVER
// via Ember OnOff in the data model and/or a code-driven OnOff server for MQTT On/Off in and out.

#include "cluster_adapter.h"

#include <app-common/zap-generated/cluster-objects.h>

using namespace chip::app::Clusters;

namespace {

constexpr MatterMqttAttributeEntry kOnOffAttributes[] = {
    { "OnOff", OnOff::Attributes::OnOff::Id, MatterMqttAttributeQuantity::kScalar },
};

constexpr const char * kOnAliases[]  = { nullptr };
constexpr const char * kOffAliases[] = { nullptr };
constexpr const char * kToggleAliases[] = { "toggle", nullptr };

constexpr MatterMqttCommandEntry kOnOffCommands[] = {
    { "On", kOnAliases, OnOff::Commands::On::Id, MatterMqttBridge::kCommandFlagNone, nullptr, 0 },
    { "Off", kOffAliases, OnOff::Commands::Off::Id, MatterMqttBridge::kCommandFlagNone, nullptr, 0 },
    { "Toggle", kToggleAliases, OnOff::Commands::Toggle::Id, MatterMqttBridge::kCommandFlagNone, nullptr, 0 },
};

} // namespace

const ClusterAdapter kOnOffAdapter = {
    OnOff::Id,
    "OnOff",
    kOnOffAttributes,
    sizeof(kOnOffAttributes) / sizeof(kOnOffAttributes[0]),
    kOnOffCommands,
    sizeof(kOnOffCommands) / sizeof(kOnOffCommands[0]),
    nullptr,
};

#endif // ZCL_USING_ON_OFF_CLUSTER_SERVER
