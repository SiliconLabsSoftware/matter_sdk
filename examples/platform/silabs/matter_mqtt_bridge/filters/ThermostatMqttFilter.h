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

#if defined(ZCL_USING_THERMOSTAT_CLUSTER_SERVER)

#include <matter_mqtt_cluster_filter.h>

#include <app-common/zap-generated/cluster-objects.h>

using namespace chip::app::Clusters;

namespace {

constexpr MatterMqttFilterAttributeEntry kThermostatAttributes[] = {
    { Thermostat::Attributes::LocalTemperature::Id },
    { Thermostat::Attributes::OccupiedHeatingSetpoint::Id },
    { Thermostat::Attributes::OccupiedCoolingSetpoint::Id },
    { Thermostat::Attributes::SystemMode::Id },
};

constexpr MatterMqttFilterCommandEntry kThermostatCommands[] = {
    { Thermostat::Commands::SetpointRaiseLower::Id, MatterMqttBridge::kCommandFlagNone },
};

} // namespace

const MatterMqttClusterFilter kThermostatMqttClusterFilter = { Thermostat::Id,
                                                               kThermostatAttributes,
                                                               sizeof(kThermostatAttributes) / sizeof(kThermostatAttributes[0]),
                                                               kThermostatCommands,
                                                               sizeof(kThermostatCommands) / sizeof(kThermostatCommands[0]) };

#endif // ZCL_USING_THERMOSTAT_CLUSTER_SERVER
