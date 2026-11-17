/*
 * Thermostat MQTT cluster adapter. Included from MqttAdapterRegistry.cpp (single TU).
 */
#pragma once

#if defined(ZCL_USING_THERMOSTAT_CLUSTER_SERVER)

#include <cluster_adapter.h>

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
    { Thermostat::Attributes::LocalTemperature::Id, MatterMqttAttributeQuantity::kTemperatureDeciDegrees },
    { Thermostat::Attributes::OccupiedHeatingSetpoint::Id, MatterMqttAttributeQuantity::kTemperatureDeciDegrees },
    { Thermostat::Attributes::OccupiedCoolingSetpoint::Id, MatterMqttAttributeQuantity::kTemperatureDeciDegrees },
    { Thermostat::Attributes::SystemMode::Id, MatterMqttAttributeQuantity::kEnum },
};

constexpr MatterMqttArgField kSetpointRaiseLowerArgs[] = {
    { "mode", 0, MatterMqttArgKind::kUnsignedInteger },
    { "amount", 1, MatterMqttArgKind::kSignedInteger },
};

constexpr MatterMqttCommandEntry kThermostatCommands[] = {
    { Thermostat::Commands::SetpointRaiseLower::Id, MatterMqttBridge::kCommandFlagNone, kSetpointRaiseLowerArgs,
      sizeof(kSetpointRaiseLowerArgs) / sizeof(kSetpointRaiseLowerArgs[0]) },
};

} // namespace

const ClusterAdapter kThermostatAdapter = { Thermostat::Id,
                                            kThermostatAttributes,
                                            sizeof(kThermostatAttributes) / sizeof(kThermostatAttributes[0]),
                                            kThermostatCommands,
                                            sizeof(kThermostatCommands) / sizeof(kThermostatCommands[0]),
                                            ThermostatSystemModeName };

#endif // ZCL_USING_THERMOSTAT_CLUSTER_SERVER
