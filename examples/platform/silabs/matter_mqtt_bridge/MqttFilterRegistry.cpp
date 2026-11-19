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

#include <MatterMqttClusterFilter.h>
#include <app-common/zap-generated/cluster-objects.h>
#include <zap-generated/gen_config.h>

namespace {

using namespace chip::app::Clusters;

#if defined(ZCL_USING_ON_OFF_CLUSTER_SERVER)
constexpr MatterMqttAttributeFilter kOnOffAttributes[] = {
    { OnOff::Attributes::OnOff::Id },
};

constexpr MatterMqttCommandFilter kOnOffCommands[] = {
    { OnOff::Commands::On::Id },
    { OnOff::Commands::Off::Id },
    { OnOff::Commands::Toggle::Id },
};
#endif

#if defined(ZCL_USING_THERMOSTAT_CLUSTER_SERVER)
constexpr MatterMqttAttributeFilter kThermostatAttributes[] = {
    { Thermostat::Attributes::LocalTemperature::Id },
    { Thermostat::Attributes::OccupiedHeatingSetpoint::Id },
    { Thermostat::Attributes::OccupiedCoolingSetpoint::Id },
    { Thermostat::Attributes::SystemMode::Id },
};

constexpr MatterMqttCommandFilter kThermostatCommands[] = {
    { Thermostat::Commands::SetpointRaiseLower::Id },
};
#endif

#if defined(ZCL_USING_DOOR_LOCK_CLUSTER_SERVER)
// Attributes only: timed-invoke commands (e.g. LockDoor) are not MQTT-injectable.
constexpr MatterMqttAttributeFilter kDoorLockAttributes[] = {
    { DoorLock::Attributes::LockState::Id },
};
#endif

#if defined(ZCL_USING_WINDOW_COVERING_CLUSTER_SERVER)
constexpr MatterMqttAttributeFilter kWindowAttributes[] = {
    { WindowCovering::Attributes::CurrentPositionLiftPercent100ths::Id },
    { WindowCovering::Attributes::CurrentPositionTiltPercent100ths::Id },
};

constexpr MatterMqttCommandFilter kWindowCommands[] = {
    { WindowCovering::Commands::GoToLiftPercentage::Id },
    { WindowCovering::Commands::GoToTiltPercentage::Id },
};
#endif

#if defined(ZCL_USING_ON_OFF_CLUSTER_SERVER) || defined(ZCL_USING_THERMOSTAT_CLUSTER_SERVER) || \
    defined(ZCL_USING_DOOR_LOCK_CLUSTER_SERVER) || defined(ZCL_USING_WINDOW_COVERING_CLUSTER_SERVER)
const MatterMqttClusterFilter kPlatformMqttClusterFilters[] = {
#if defined(ZCL_USING_ON_OFF_CLUSTER_SERVER)
    { OnOff::Id, kOnOffAttributes, sizeof(kOnOffAttributes) / sizeof(kOnOffAttributes[0]), kOnOffCommands,
      sizeof(kOnOffCommands) / sizeof(kOnOffCommands[0]) },
#endif
#if defined(ZCL_USING_THERMOSTAT_CLUSTER_SERVER)
    { Thermostat::Id, kThermostatAttributes, sizeof(kThermostatAttributes) / sizeof(kThermostatAttributes[0]), kThermostatCommands,
      sizeof(kThermostatCommands) / sizeof(kThermostatCommands[0]) },
#endif
#if defined(ZCL_USING_DOOR_LOCK_CLUSTER_SERVER)
    { DoorLock::Id, kDoorLockAttributes, sizeof(kDoorLockAttributes) / sizeof(kDoorLockAttributes[0]), nullptr, 0 },
#endif
#if defined(ZCL_USING_WINDOW_COVERING_CLUSTER_SERVER)
    { WindowCovering::Id, kWindowAttributes, sizeof(kWindowAttributes) / sizeof(kWindowAttributes[0]), kWindowCommands,
      sizeof(kWindowCommands) / sizeof(kWindowCommands[0]) },
#endif
};
#endif

} // namespace

chip::Span<const MatterMqttClusterFilter> GetPlatformMqttClusterFilters()
{
#if defined(ZCL_USING_ON_OFF_CLUSTER_SERVER) || defined(ZCL_USING_THERMOSTAT_CLUSTER_SERVER) || \
    defined(ZCL_USING_DOOR_LOCK_CLUSTER_SERVER) || defined(ZCL_USING_WINDOW_COVERING_CLUSTER_SERVER)
    return chip::Span<const MatterMqttClusterFilter>(kPlatformMqttClusterFilters);
#else
    return {};
#endif
}
