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

#if defined(ZCL_USING_THERMOSTAT_CLUSTER_SERVER)

#include "cluster_adapter.h"

#include <app-common/zap-generated/cluster-objects.h>

using namespace chip::app::Clusters;

namespace {

const char * ThermostatSystemModeName(chip::AttributeId attrId, uint32_t raw)
{
    if (attrId != Thermostat::Attributes::SystemMode::Id)
    {
        return nullptr;
    }
    switch (static_cast<Thermostat::SystemModeEnum>(raw))
    {
    case Thermostat::SystemModeEnum::kOff:
        return "Off";
    case Thermostat::SystemModeEnum::kAuto:
        return "Auto";
    case Thermostat::SystemModeEnum::kCool:
        return "Cool";
    case Thermostat::SystemModeEnum::kHeat:
        return "Heat";
    case Thermostat::SystemModeEnum::kEmergencyHeat:
        return "EmergencyHeat";
    case Thermostat::SystemModeEnum::kPrecooling:
        return "Precooling";
    case Thermostat::SystemModeEnum::kFanOnly:
        return "FanOnly";
    case Thermostat::SystemModeEnum::kDry:
        return "Dry";
    case Thermostat::SystemModeEnum::kSleep:
        return "Sleep";
    default:
        return "Unknown";
    }
}

constexpr MatterMqttAttributeEntry kThermostatAttributes[] = {
    { "LocalTemperature", Thermostat::Attributes::LocalTemperature::Id, MatterMqttAttributeQuantity::kTemperatureDeciDegrees },
    { "OccupiedHeatingSetpoint", Thermostat::Attributes::OccupiedHeatingSetpoint::Id,
      MatterMqttAttributeQuantity::kTemperatureDeciDegrees },
    { "OccupiedCoolingSetpoint", Thermostat::Attributes::OccupiedCoolingSetpoint::Id,
      MatterMqttAttributeQuantity::kTemperatureDeciDegrees },
    { "SystemMode", Thermostat::Attributes::SystemMode::Id, MatterMqttAttributeQuantity::kEnum },
};

constexpr MatterMqttArgField kSetpointRaiseLowerArgs[] = {
    { "mode", 0, chip::TLV::TLVType::kTLVType_UnsignedInteger },
    { "amount", 1, chip::TLV::TLVType::kTLVType_SignedInteger },
};

constexpr MatterMqttCommandEntry kThermostatCommands[] = {
    { "SetpointRaiseLower", nullptr, Thermostat::Commands::SetpointRaiseLower::Id, MatterMqttBridge::kCommandFlagNone,
      kSetpointRaiseLowerArgs, sizeof(kSetpointRaiseLowerArgs) / sizeof(kSetpointRaiseLowerArgs[0]) },
};

} // namespace

const ClusterAdapter kThermostatAdapter = {
    Thermostat::Id,
    "Thermostat",
    kThermostatAttributes,
    sizeof(kThermostatAttributes) / sizeof(kThermostatAttributes[0]),
    kThermostatCommands,
    sizeof(kThermostatCommands) / sizeof(kThermostatCommands[0]),
    ThermostatSystemModeName,
};

#endif // ZCL_USING_THERMOSTAT_CLUSTER_SERVER
