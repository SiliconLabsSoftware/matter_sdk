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

#include "ThermostatMqttBridgeConfig.h"

#include <app-common/zap-generated/cluster-objects.h>
#include <lib/support/CodeUtils.h>

namespace {

using namespace chip;
using namespace chip::app::Clusters;

// TODO(mqtt-bridge): OnOff MQTT paths require ZCL_USING_ON_OFF_CLUSTER_SERVER (ZAP and/or code-driven).

constexpr EndpointId kThermostatEndpoint = 1;

// Product ACL: which Matter paths may be mirrored over MQTT (independent of adapter table size).

bool IsAdminCluster(ClusterId cluster)
{
    return cluster == AccessControl::Id || cluster == OperationalCredentials::Id || cluster == GroupKeyManagement::Id ||
        cluster == AdministratorCommissioning::Id;
}

// Outbound reports: AttributeChangeListener + snapshot only for whitelisted attributes.
bool ThermostatAllowsPublish(EndpointId ep, ClusterId cluster, AttributeId attr)
{
    if (IsAdminCluster(cluster))
    {
        return false;
    }

    if (ep == kThermostatEndpoint && cluster == Thermostat::Id)
    {
        return attr == Thermostat::Attributes::LocalTemperature::Id ||
            attr == Thermostat::Attributes::OccupiedHeatingSetpoint::Id ||
            attr == Thermostat::Attributes::OccupiedCoolingSetpoint::Id || attr == Thermostat::Attributes::SystemMode::Id;
    }

#if defined(ZCL_USING_ON_OFF_CLUSTER_SERVER)
    if (ep == kThermostatEndpoint && cluster == OnOff::Id && attr == OnOff::Attributes::OnOff::Id)
    {
        return true;
    }
#endif

    return false;
}

// Inbound commands: checked after topic/JSON parse, before PackCommandArgs/invoke.
bool ThermostatAllowsCommand(EndpointId ep, ClusterId cluster, CommandId cmd)
{
    if (IsAdminCluster(cluster))
    {
        return false;
    }

    if (ep != kThermostatEndpoint)
    {
        return false;
    }

#if defined(ZCL_USING_ON_OFF_CLUSTER_SERVER)
    if (cluster == OnOff::Id)
    {
        return cmd == OnOff::Commands::On::Id || cmd == OnOff::Commands::Off::Id || cmd == OnOff::Commands::Toggle::Id;
    }
#endif

#if defined(ZCL_USING_THERMOSTAT_CLUSTER_SERVER)
    if (cluster == Thermostat::Id)
    {
        return cmd == Thermostat::Commands::SetpointRaiseLower::Id;
    }
#endif

#if defined(ZCL_USING_DOOR_LOCK_CLUSTER_SERVER)
    if (cluster == DoorLock::Id)
    {
        if (cmd == DoorLock::Commands::UnlockDoor::Id)
        {
            return false;
        }
        return cmd == DoorLock::Commands::LockDoor::Id;
    }
#endif

    (void) cmd;
    return false;
}

// Published once after command subscribe succeeds (see StartSnapshot in MatterMqttBridge.cpp).
constexpr MatterMqttSnapshotPath kThermostatSnapshotPaths[] = {
#if defined(ZCL_USING_THERMOSTAT_CLUSTER_SERVER)
    { kThermostatEndpoint, Thermostat::Id, Thermostat::Attributes::LocalTemperature::Id },
    { kThermostatEndpoint, Thermostat::Id, Thermostat::Attributes::OccupiedHeatingSetpoint::Id },
    { kThermostatEndpoint, Thermostat::Id, Thermostat::Attributes::OccupiedCoolingSetpoint::Id },
    { kThermostatEndpoint, Thermostat::Id, Thermostat::Attributes::SystemMode::Id },
#endif
#if defined(ZCL_USING_ON_OFF_CLUSTER_SERVER)
    { kThermostatEndpoint, OnOff::Id, OnOff::Attributes::OnOff::Id },
#endif
};

} // namespace

const DeviceMqttProfile kThermostatMqttProfile = {
    ThermostatAllowsPublish,
    ThermostatAllowsCommand,
    kThermostatSnapshotPaths,
    sizeof(kThermostatSnapshotPaths) / sizeof(kThermostatSnapshotPaths[0]),
};

const MatterMqttBridgeConfig kThermostatBridgeConfig = { nullptr, nullptr, &gThermostatMqttAdapterRegistry,
                                                         &kThermostatMqttProfile, nullptr, nullptr, nullptr, 0, 0, false };
