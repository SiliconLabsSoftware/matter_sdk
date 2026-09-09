/*
 *
 *    Copyright (c) 2026 Project CHIP Authors
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

#include "include/AppCommandDelegate.h"

#include <app-common/zap-generated/cluster-objects.h>
#include <app/clusters/basic-information/BasicInformationCluster.h>
#include <app/clusters/boolean-state-server/BooleanStateCluster.h>
#include <app/clusters/electrical-energy-measurement-server/ElectricalEnergyMeasurementCluster.h>
#include <app/clusters/mode-select-server/ModeSelectCluster.h>
#include <app/clusters/occupancy-sensor-server/OccupancySensingCluster.h>
#include <app/clusters/on-off-server/OnOffCluster.h>
#include <platform/PlatformManager.h>

using namespace chip;
using namespace chip::app;

namespace {

struct CommandContext
{
    Json::Value value;
    EndpointId endpointId;
    AllDevicesAppCommandDelegate * delegate;
    AllDevicesAppNamedPipeCommandHandler * handler;
};

class IncreaseConfigurationVersionCommandHandler : public AllDevicesAppNamedPipeCommandHandler
{
public:
    const char * GetName() const override { return "IncreaseConfigurationVersion"; }
    void Handle(const Json::Value & json, AllDevicesAppCommandDelegate * delegate, EndpointId endpointId) override
    {
        auto * cluster =
            delegate->GetClusterImplementationRegistry().GetClusterByEndpoint<chip::app::Clusters::BasicInformationCluster>(
                endpointId);
        if (!cluster)
        {
            ChipLogError(AppServer, "BasicInformationCluster not found on endpoint %d", endpointId);
            return;
        }

        CHIP_ERROR err = cluster->IncreaseConfigurationVersion();
        ChipLogProgress(AppServer, "IncreaseConfigurationVersion on endpoint %d: %" CHIP_ERROR_FORMAT, endpointId, err.Format());
    }
};

class SetOccupancyCommandHandler : public AllDevicesAppNamedPipeCommandHandler
{
public:
    const char * GetName() const override { return "SetOccupancy"; }
    void Handle(const Json::Value & json, AllDevicesAppCommandDelegate * delegate, EndpointId endpointId) override
    {
        auto * cluster =
            delegate->GetClusterImplementationRegistry().GetClusterByEndpoint<chip::app::Clusters::OccupancySensingCluster>(
                endpointId);
        if (!cluster)
        {
            ChipLogError(AppServer, "OccupancySensingCluster not found on endpoint %d", endpointId);
            return;
        }

        if (!json.isMember("Occupancy") || !json["Occupancy"].isUInt())
        {
            ChipLogError(AppServer, "Invalid SetOccupancy command: missing 'Occupancy' field");
            return;
        }

        unsigned int occupancyVal = json["Occupancy"].asUInt();
        if (occupancyVal != 0 && occupancyVal != 1)
        {
            ChipLogError(AppServer, "Invalid occupancy value: %u", occupancyVal);
            return;
        }
        uint8_t occupancy = static_cast<uint8_t>(occupancyVal);

        cluster->SetOccupancy(occupancy != 0);
        ChipLogProgress(AppServer, "SetOccupancy to %d on endpoint %d", occupancy, endpointId);
    }
};

class SetHoldTimeCommandHandler : public AllDevicesAppNamedPipeCommandHandler
{
public:
    const char * GetName() const override { return "SetHoldTime"; }
    void Handle(const Json::Value & json, AllDevicesAppCommandDelegate * delegate, EndpointId endpointId) override
    {
        auto * cluster =
            delegate->GetClusterImplementationRegistry().GetClusterByEndpoint<chip::app::Clusters::OccupancySensingCluster>(
                endpointId);
        if (!cluster)
        {
            ChipLogError(AppServer, "OccupancySensingCluster not found on endpoint %d", endpointId);
            return;
        }

        if (!json.isMember("HoldTime") || !json["HoldTime"].isUInt())
        {
            ChipLogError(AppServer, "Invalid SetHoldTime command: missing 'HoldTime' field");
            return;
        }

        unsigned int holdTimeVal = json["HoldTime"].asUInt();
        if (holdTimeVal > 0xFFFF)
        {
            ChipLogError(AppServer, "Invalid HoldTime value (out of range): %u", holdTimeVal);
            return;
        }
        uint16_t holdTime = static_cast<uint16_t>(holdTimeVal);
        cluster->SetHoldTime(holdTime);
        ChipLogProgress(AppServer, "SetHoldTime to %d on endpoint %d", holdTime, endpointId);
    }
};

class SetBooleanStateCommandHandler : public AllDevicesAppNamedPipeCommandHandler
{
public:
    const char * GetName() const override { return "SetBooleanState"; }
    void Handle(const Json::Value & json, AllDevicesAppCommandDelegate * delegate, EndpointId endpointId) override
    {
        auto * cluster =
            delegate->GetClusterImplementationRegistry().GetClusterByEndpoint<chip::app::Clusters::BooleanStateCluster>(endpointId);
        if (!cluster)
        {
            ChipLogError(AppServer, "BooleanStateCluster not found on endpoint %d", endpointId);
            return;
        }

        if (!json.isMember("NewState") || !json["NewState"].isBool())
        {
            ChipLogError(AppServer, "Invalid SetBooleanState command: missing 'NewState' field");
            return;
        }

        bool newState = json["NewState"].asBool();
        cluster->SetStateValue(newState);
        ChipLogProgress(AppServer, "SetBooleanState to %d on endpoint %d", newState, endpointId);
    }
};

class SetOnOffCommandHandler : public AllDevicesAppNamedPipeCommandHandler
{
public:
    const char * GetName() const override { return "SetOnOff"; }
    void Handle(const Json::Value & json, AllDevicesAppCommandDelegate * delegate, EndpointId endpointId) override
    {
        auto * cluster =
            delegate->GetClusterImplementationRegistry().GetClusterByEndpoint<chip::app::Clusters::OnOffCluster>(endpointId);
        if (!cluster)
        {
            ChipLogError(AppServer, "OnOffCluster not found on endpoint %d", endpointId);
            return;
        }

        if (!json.isMember("OnOff") || !json["OnOff"].isBool())
        {
            ChipLogError(AppServer, "Invalid SetOnOff command: missing 'OnOff' field");
            return;
        }

        bool onOff     = json["OnOff"].asBool();
        CHIP_ERROR err = cluster->SetOnOff(onOff);
        ChipLogProgress(AppServer, "SetOnOff to %d on endpoint %d: %" CHIP_ERROR_FORMAT, onOff, endpointId, err.Format());
    }
};

class RvcResetCommandHandler : public AllDevicesAppNamedPipeCommandHandler
{
public:
    const char * GetName() const override { return "Reset"; }
    void Handle(const Json::Value & json, AllDevicesAppCommandDelegate * delegate, EndpointId endpointId) override
    {
        auto * simulation = GetRvcSimulation(delegate, endpointId);
        if (simulation == nullptr)
        {
            return;
        }
        simulation->HandleReset();
    }
};

class RvcChargedCommandHandler : public AllDevicesAppNamedPipeCommandHandler
{
public:
    const char * GetName() const override { return "Charged"; }
    void Handle(const Json::Value & json, AllDevicesAppCommandDelegate * delegate, EndpointId endpointId) override
    {
        auto * simulation = GetRvcSimulation(delegate, endpointId);
        if (simulation == nullptr)
        {
            return;
        }
        simulation->HandleCharged();
    }
};

class RvcChargingCommandHandler : public AllDevicesAppNamedPipeCommandHandler
{
public:
    const char * GetName() const override { return "Charging"; }
    void Handle(const Json::Value & json, AllDevicesAppCommandDelegate * delegate, EndpointId endpointId) override
    {
        auto * simulation = GetRvcSimulation(delegate, endpointId);
        if (simulation == nullptr)
        {
            return;
        }
        simulation->HandleCharging();
    }
};

class RvcDockedCommandHandler : public AllDevicesAppNamedPipeCommandHandler
{
public:
    const char * GetName() const override { return "Docked"; }
    void Handle(const Json::Value & json, AllDevicesAppCommandDelegate * delegate, EndpointId endpointId) override
    {
        auto * simulation = GetRvcSimulation(delegate, endpointId);
        if (simulation == nullptr)
        {
            return;
        }
        simulation->HandleDocked();
    }
};

class RvcChargerFoundCommandHandler : public AllDevicesAppNamedPipeCommandHandler
{
public:
    const char * GetName() const override { return "ChargerFound"; }
    void Handle(const Json::Value & json, AllDevicesAppCommandDelegate * delegate, EndpointId endpointId) override
    {
        auto * simulation = GetRvcSimulation(delegate, endpointId);
        if (simulation == nullptr)
        {
            return;
        }
        simulation->HandleChargerFound();
    }
};

class RvcLowChargeCommandHandler : public AllDevicesAppNamedPipeCommandHandler
{
public:
    const char * GetName() const override { return "LowCharge"; }
    void Handle(const Json::Value & json, AllDevicesAppCommandDelegate * delegate, EndpointId endpointId) override
    {
        auto * simulation = GetRvcSimulation(delegate, endpointId);
        if (simulation == nullptr)
        {
            return;
        }
        simulation->HandleLowCharge();
    }
};

class RvcActivityCompleteCommandHandler : public AllDevicesAppNamedPipeCommandHandler
{
public:
    const char * GetName() const override { return "ActivityComplete"; }
    void Handle(const Json::Value & json, AllDevicesAppCommandDelegate * delegate, EndpointId endpointId) override
    {
        auto * simulation = GetRvcSimulation(delegate, endpointId);
        if (simulation == nullptr)
        {
            return;
        }
        simulation->HandleActivityComplete();
    }
};

class RvcAreaCompleteCommandHandler : public AllDevicesAppNamedPipeCommandHandler
{
public:
    const char * GetName() const override { return "AreaComplete"; }
    void Handle(const Json::Value & json, AllDevicesAppCommandDelegate * delegate, EndpointId endpointId) override
    {
        auto * simulation = GetRvcSimulation(delegate, endpointId);
        if (simulation == nullptr)
        {
            return;
        }
        simulation->HandleAreaComplete();
    }
};

class RvcClearErrorCommandHandler : public AllDevicesAppNamedPipeCommandHandler
{
public:
    const char * GetName() const override { return "ClearError"; }
    void Handle(const Json::Value & json, AllDevicesAppCommandDelegate * delegate, EndpointId endpointId) override
    {
        auto * simulation = GetRvcSimulation(delegate, endpointId);
        if (simulation == nullptr)
        {
            return;
        }
        simulation->HandleClearError();
    }
};

class RvcEmptyingDustBinCommandHandler : public AllDevicesAppNamedPipeCommandHandler
{
public:
    const char * GetName() const override { return "EmptyingDustBin"; }
    void Handle(const Json::Value & json, AllDevicesAppCommandDelegate * delegate, EndpointId endpointId) override
    {
        auto * operationalState = GetClusterByEndpoint<Clusters::RvcOperationalState::RvcOperationalStateCluster>(
            delegate, endpointId, "RvcOperationalState");
        if (operationalState == nullptr)
        {
            return;
        }
        LogErrorOnFailure(
            operationalState->SetOperationalState(to_underlying(RvcOperationalState::OperationalStateEnum::kEmptyingDustBin)));
    }
};

class RvcCleaningMopCommandHandler : public AllDevicesAppNamedPipeCommandHandler
{
public:
    const char * GetName() const override { return "CleaningMop"; }
    void Handle(const Json::Value & json, AllDevicesAppCommandDelegate * delegate, EndpointId endpointId) override
    {
        auto * operationalState = GetClusterByEndpoint<Clusters::RvcOperationalState::RvcOperationalStateCluster>(
            delegate, endpointId, "RvcOperationalState");
        if (operationalState == nullptr)
        {
            return;
        }
        LogErrorOnFailure(
            operationalState->SetOperationalState(to_underlying(RvcOperationalState::OperationalStateEnum::kCleaningMop)));
    }
};

class RvcFillingWaterTankCommandHandler : public AllDevicesAppNamedPipeCommandHandler
{
public:
    const char * GetName() const override { return "FillingWaterTank"; }
    void Handle(const Json::Value & json, AllDevicesAppCommandDelegate * delegate, EndpointId endpointId) override
    {
        auto * operationalState = GetClusterByEndpoint<Clusters::RvcOperationalState::RvcOperationalStateCluster>(
            delegate, endpointId, "RvcOperationalState");
        if (operationalState == nullptr)
        {
            return;
        }
        LogErrorOnFailure(
            operationalState->SetOperationalState(to_underlying(RvcOperationalState::OperationalStateEnum::kFillingWaterTank)));
    }
};

class RvcUpdatingMapsCommandHandler : public AllDevicesAppNamedPipeCommandHandler
{
public:
    const char * GetName() const override { return "UpdatingMaps"; }
    void Handle(const Json::Value & json, AllDevicesAppCommandDelegate * delegate, EndpointId endpointId) override
    {
        auto * operationalState = GetClusterByEndpoint<Clusters::RvcOperationalState::RvcOperationalStateCluster>(
            delegate, endpointId, "RvcOperationalState");
        if (operationalState == nullptr)
        {
            return;
        }
        LogErrorOnFailure(
            operationalState->SetOperationalState(to_underlying(RvcOperationalState::OperationalStateEnum::kUpdatingMaps)));
    }
};

class RvcErrorEventCommandHandler : public AllDevicesAppNamedPipeCommandHandler
{
public:
    const char * GetName() const override { return "ErrorEvent"; }
    void Handle(const Json::Value & json, AllDevicesAppCommandDelegate * delegate, EndpointId endpointId) override
    {
        auto * simulation = GetRvcSimulation(delegate, endpointId);
        if (simulation == nullptr)
        {
            return;
        }
        if (json.isMember("Error") && json["Error"].isString())
        {
            simulation->HandleErrorEvent(json["Error"].asString());
        }
    }
};

class RvcAddMapCommandHandler : public AllDevicesAppNamedPipeCommandHandler
{
public:
    const char * GetName() const override { return "AddMap"; }
    void Handle(const Json::Value & json, AllDevicesAppCommandDelegate * delegate, EndpointId endpointId) override
    {
        auto * serviceArea = GetClusterByEndpoint<Clusters::ServiceArea::ServiceAreaCluster>(delegate, endpointId, "ServiceArea");
        if (serviceArea == nullptr)
        {
            return;
        }
        if (json.isMember("MapId") && json["MapId"].isUInt() && json.isMember("MapName") && json["MapName"].isString())
        {
            const uint32_t mapId = json["MapId"].asUInt();
            std::string mapName  = json["MapName"].asString();
            if (!serviceArea->AddSupportedMap(mapId, CharSpan(mapName.data(), mapName.size())))
            {
                ChipLogError(AppServer, "AddMap: failed to add map %u", static_cast<unsigned>(mapId));
            }
        }
    }
};

class RvcRemoveMapCommandHandler : public AllDevicesAppNamedPipeCommandHandler
{
public:
    const char * GetName() const override { return "RemoveMap"; }
    void Handle(const Json::Value & json, AllDevicesAppCommandDelegate * delegate, EndpointId endpointId) override
    {
        auto * serviceArea = GetClusterByEndpoint<Clusters::ServiceArea::ServiceAreaCluster>(delegate, endpointId, "ServiceArea");
        if (serviceArea == nullptr)
        {
            return;
        }
        if (json.isMember("MapId") && json["MapId"].isUInt())
        {
            const uint32_t mapId = json["MapId"].asUInt();
            if (!serviceArea->RemoveSupportedMap(mapId))
            {
                ChipLogError(AppServer, "RemoveMap: failed to remove map %u", static_cast<unsigned>(mapId));
            }
        }
    }
};

class RvcAddAreaCommandHandler : public AllDevicesAppNamedPipeCommandHandler
{
public:
    const char * GetName() const override { return "AddArea"; }
    void Handle(const Json::Value & json, AllDevicesAppCommandDelegate * delegate, EndpointId endpointId) override
    {
        auto * serviceArea = GetClusterByEndpoint<Clusters::ServiceArea::ServiceAreaCluster>(delegate, endpointId, "ServiceArea");
        if (serviceArea == nullptr)
        {
            return;
        }
        if (!json.isMember("AreaId") || !json["AreaId"].isUInt())
        {
            return;
        }

        const uint32_t areaId = json["AreaId"].asUInt();
        ServiceArea::AreaStructureWrapper area;
        area.SetAreaId(areaId);
        if (json.isMember("MapId"))
        {
            if (!json["MapId"].isUInt())
            {
                return;
            }
            area.SetMapId(json["MapId"].asUInt());
        }
        if (json.isMember("LocationName"))
        {
            if (!json["LocationName"].isString())
            {
                return;
            }
            std::string locationName = json["LocationName"].asString();
            area.SetLocationInfo(CharSpan(locationName.data(), locationName.size()), DataModel::NullNullable,
                                 DataModel::NullNullable);
        }
        if (!serviceArea->AddSupportedArea(area))
        {
            ChipLogError(AppServer, "AddArea: failed to add area %u", static_cast<unsigned>(areaId));
        }
    }
};

class RvcRemoveAreaCommandHandler : public AllDevicesAppNamedPipeCommandHandler
{
public:
    const char * GetName() const override { return "RemoveArea"; }
    void Handle(const Json::Value & json, AllDevicesAppCommandDelegate * delegate, EndpointId endpointId) override
    {
        auto * serviceArea = GetClusterByEndpoint<Clusters::ServiceArea::ServiceAreaCluster>(delegate, endpointId, "ServiceArea");
        if (serviceArea == nullptr)
        {
            return;
        }
        if (json.isMember("AreaId") && json["AreaId"].isUInt())
        {
            const uint32_t areaId = json["AreaId"].asUInt();
            if (!serviceArea->RemoveSupportedArea(areaId))
            {
                ChipLogError(AppServer, "RemoveArea: failed to remove area %u", static_cast<unsigned>(areaId));
            }
        }
    }
};

/**
 * Named pipe handler for generating a electrical energy measurement snapshots on the ElectricalEnergyMeasurement cluster
 *
 * Usage example:
 *   echo '{"Name":"GenerateElectricalEnergyMeasurementSnapshots","EndpointId":1}'> /tmp/acs_fifo
 *
 * JSON Arguments:
 *   - "Name": Must be "GenerateElectricalEnergyMeasurementSnapshots"
 *   - "EndpointId": ID of endpoint
 *
 * @param jsonValue - JSON payload from named pipe
 */
class GenerateElectricalEnergyMeasurementSnapshotsCommandHandler : public AllDevicesAppNamedPipeCommandHandler
{
public:
    const char * GetName() const override { return "GenerateElectricalEnergyMeasurementSnapshots"; }
    void Handle(const Json::Value & json, AllDevicesAppCommandDelegate * delegate, EndpointId endpointId) override
    {
        auto * cluster =
            delegate->GetClusterImplementationRegistry()
                .GetClusterByEndpoint<chip::app::Clusters::ElectricalEnergyMeasurement::ElectricalEnergyMeasurementCluster>(
                    endpointId);
        if (!cluster)
        {
            ChipLogError(AppServer, "ElectricalEnergyMeasurementCluster not found on endpoint %d", endpointId);
            return;
        }
        cluster->GenerateSnapshots();
    }
};

class SetModeSelectCurrentModeCommandHandler : public AllDevicesAppNamedPipeCommandHandler
{
public:
    const char * GetName() const override { return "SetModeSelectCurrentMode"; }
    void Handle(const Json::Value & json, AllDevicesAppCommandDelegate * delegate, EndpointId endpointId) override
    {
        auto * cluster =
            delegate->GetClusterImplementationRegistry().GetClusterByEndpoint<chip::app::Clusters::ModeSelectCluster>(endpointId);
        if (!cluster)
        {
            ChipLogError(AppServer, "ModeSelectCluster not found on endpoint %d", endpointId);
            return;
        }

        if (!json.isMember("NewMode") || !json["NewMode"].isUInt())
        {
            ChipLogError(AppServer, "Invalid SetModeSelectCurrentMode command: missing 'NewMode' field");
            return;
        }

        unsigned int newModeVal = json["NewMode"].asUInt();
        if (newModeVal > UINT8_MAX)
        {
            ChipLogError(AppServer, "Invalid mode value (out of range): %u", newModeVal);
            return;
        }

        uint8_t newMode = static_cast<uint8_t>(newModeVal);
        if (!cluster->IsSupportedMode(newMode))
        {
            ChipLogError(AppServer, "Invalid mode: %u", newMode);
            return;
        }

        Protocols::InteractionModel::Status status = cluster->UpdateCurrentMode(newMode);
        if (status != Protocols::InteractionModel::Status::Success)
        {
            ChipLogError(AppServer, "Failed to set mode %u on endpoint %d: status %u", newMode, endpointId,
                         static_cast<unsigned>(to_underlying(status)));
            return;
        }
        ChipLogProgress(AppServer, "SetModeSelectCurrentMode to %d on endpoint %d", newMode, endpointId);
    }
};

} // namespace

void AllDevicesAppCommandDelegate::OnEventCommandReceived(const char * json)
{
    Json::Reader reader;
    Json::Value value;
    if (!reader.parse(json, value))
    {
        ChipLogError(AppServer, "Failed to parse JSON command: %s", reader.getFormattedErrorMessages().c_str());
        return;
    }

    if (!value.isMember("Name") || !value["Name"].isString() || !value.isMember("EndpointId") || !value["EndpointId"].isUInt())
    {
        ChipLogError(AppServer, "Invalid command format: %s", json);
        return;
    }

    std::string commandName = value["Name"].asString();

    unsigned int endpointIdVal = value["EndpointId"].asUInt();
    if (endpointIdVal > 0xFFFF)
    {
        ChipLogError(AppServer, "Invalid EndpointId (out of range): %u", endpointIdVal);
        return;
    }

    EndpointId endpointId = static_cast<EndpointId>(endpointIdVal);
    auto handlerIt        = mCommandHandlers.find(commandName);

    if (handlerIt == mCommandHandlers.end())
    {
        ChipLogError(AppServer, "Unknown command: %s", commandName.c_str());
        return;
    }

    auto * context = Platform::New<CommandContext>();
    if (context == nullptr)
    {
        ChipLogError(AppServer, "Failure to allocate command context! Ignoring command.");
        return;
    }
    context->value      = value;
    context->endpointId = endpointId;
    context->delegate   = this;
    context->handler    = handlerIt->second.get();

    CHIP_ERROR err = DeviceLayer::PlatformMgr().ScheduleWork(DispatchCommand, reinterpret_cast<intptr_t>(context));
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "Failed to schedule work: %" CHIP_ERROR_FORMAT, err.Format());
        Platform::Delete(context);
    }
}

void AllDevicesAppCommandDelegate::DispatchCommand(intptr_t context)
{
    auto * cmdContext = reinterpret_cast<CommandContext *>(context);
    cmdContext->handler->Handle(cmdContext->value, cmdContext->delegate, cmdContext->endpointId);
    Platform::Delete(cmdContext);
}

void AllDevicesAppCommandDelegate::RegisterCommandHandler(std::unique_ptr<AllDevicesAppNamedPipeCommandHandler> handler)
{
    mCommandHandlers[handler->GetName()] = std::move(handler);
}

void AllDevicesAppCommandDelegate::RegisterCommandHandlers()
{
    RegisterCommandHandler(std::make_unique<IncreaseConfigurationVersionCommandHandler>());
    RegisterCommandHandler(std::make_unique<SetOccupancyCommandHandler>());
    RegisterCommandHandler(std::make_unique<SetHoldTimeCommandHandler>());
    RegisterCommandHandler(std::make_unique<SetBooleanStateCommandHandler>());
    RegisterCommandHandler(std::make_unique<SetOnOffCommandHandler>());
    RegisterCommandHandler(std::make_unique<RvcResetCommandHandler>());
    RegisterCommandHandler(std::make_unique<RvcChargedCommandHandler>());
    RegisterCommandHandler(std::make_unique<RvcChargingCommandHandler>());
    RegisterCommandHandler(std::make_unique<RvcDockedCommandHandler>());
    RegisterCommandHandler(std::make_unique<RvcChargerFoundCommandHandler>());
    RegisterCommandHandler(std::make_unique<RvcLowChargeCommandHandler>());
    RegisterCommandHandler(std::make_unique<RvcActivityCompleteCommandHandler>());
    RegisterCommandHandler(std::make_unique<RvcAreaCompleteCommandHandler>());
    RegisterCommandHandler(std::make_unique<RvcClearErrorCommandHandler>());
    RegisterCommandHandler(std::make_unique<RvcEmptyingDustBinCommandHandler>());
    RegisterCommandHandler(std::make_unique<RvcCleaningMopCommandHandler>());
    RegisterCommandHandler(std::make_unique<RvcFillingWaterTankCommandHandler>());
    RegisterCommandHandler(std::make_unique<RvcUpdatingMapsCommandHandler>());
    RegisterCommandHandler(std::make_unique<RvcErrorEventCommandHandler>());
    RegisterCommandHandler(std::make_unique<RvcAddMapCommandHandler>());
    RegisterCommandHandler(std::make_unique<RvcRemoveMapCommandHandler>());
    RegisterCommandHandler(std::make_unique<RvcAddAreaCommandHandler>());
    RegisterCommandHandler(std::make_unique<RvcRemoveAreaCommandHandler>());
    RegisterCommandHandler(std::make_unique<GenerateElectricalEnergyMeasurementSnapshotsCommandHandler>());
    RegisterCommandHandler(std::make_unique<SetModeSelectCurrentModeCommandHandler>());
}
