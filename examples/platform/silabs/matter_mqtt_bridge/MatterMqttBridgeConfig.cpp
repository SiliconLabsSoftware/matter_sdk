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

#include "MatterMqttBridgeConfig.h"

#include <app-common/zap-generated/cluster-objects.h>
#include <app/data-model-provider/Provider.h>
#include <data-model-providers/codegen/CodegenDataModelProvider.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/ReadOnlyBuffer.h>
#include <server/Server.h>

namespace {

constexpr MatterMqttPublishTemplate kPublishTemplates[] = {
#if defined(ZCL_USING_THERMOSTAT_CLUSTER_SERVER)
    { chip::app::Clusters::Thermostat::Id, chip::app::Clusters::Thermostat::Attributes::LocalTemperature::Id },
    { chip::app::Clusters::Thermostat::Id, chip::app::Clusters::Thermostat::Attributes::OccupiedHeatingSetpoint::Id },
    { chip::app::Clusters::Thermostat::Id, chip::app::Clusters::Thermostat::Attributes::OccupiedCoolingSetpoint::Id },
    { chip::app::Clusters::Thermostat::Id, chip::app::Clusters::Thermostat::Attributes::SystemMode::Id },
#endif
#if defined(ZCL_USING_ON_OFF_CLUSTER_SERVER)
    { chip::app::Clusters::OnOff::Id, chip::app::Clusters::OnOff::Attributes::OnOff::Id },
#endif
#if defined(ZCL_USING_DOOR_LOCK_CLUSTER_SERVER)
    { chip::app::Clusters::DoorLock::Id, chip::app::Clusters::DoorLock::Attributes::LockState::Id },
#endif
#if defined(ZCL_USING_WINDOW_COVERING_CLUSTER_SERVER)
    { chip::app::Clusters::WindowCovering::Id,
      chip::app::Clusters::WindowCovering::Attributes::CurrentPositionLiftPercent100ths::Id },
    { chip::app::Clusters::WindowCovering::Id,
      chip::app::Clusters::WindowCovering::Attributes::CurrentPositionTiltPercent100ths::Id },
#endif
};

constexpr size_t kPublishTemplateCount = sizeof(kPublishTemplates) / sizeof(kPublishTemplates[0]);

} // namespace

bool PlatformDeviceMqttProfile::MatchesPublishTemplate(chip::ClusterId cluster, chip::AttributeId attribute)
{
    for (size_t i = 0; i < kPublishTemplateCount; ++i)
    {
        if (kPublishTemplates[i].clusterId == cluster && kPublishTemplates[i].attributeId == attribute)
        {
            return true;
        }
    }
    return false;
}

void PlatformDeviceMqttProfile::EnsureSnapshotPathsBuilt() const
{
    if (mSnapshotPathsBuilt)
    {
        return;
    }

    mSnapshotPathCount = 0;
    chip::ReadOnlyBufferBuilder<chip::app::DataModel::EndpointEntry> endpoints;
    if (chip::app::CodegenDataModelProvider::Instance().Endpoints(endpoints) != CHIP_NO_ERROR)
    {
        mSnapshotPathsBuilt = true;
        return;
    }

    for (const chip::app::DataModel::EndpointEntry & endpoint : endpoints.TakeBuffer())
    {
        for (size_t t = 0; t < kPublishTemplateCount; ++t)
        {
            if (mSnapshotPathCount >= MATTER_MQTT_MAX_SNAPSHOT_PATHS)
            {
                mSnapshotPathsBuilt = true;
                return;
            }
            mSnapshotPaths[mSnapshotPathCount].endpoint    = endpoint.id;
            mSnapshotPaths[mSnapshotPathCount].clusterId     = kPublishTemplates[t].clusterId;
            mSnapshotPaths[mSnapshotPathCount].attributeId = kPublishTemplates[t].attributeId;
            ++mSnapshotPathCount;
        }
    }
    mSnapshotPathsBuilt = true;
}

bool PlatformDeviceMqttProfile::IsAttributeAllowed(chip::EndpointId endpoint, chip::ClusterId cluster,
                                                   chip::AttributeId attribute) const
{
    if (!IsEndpointAllowed(endpoint))
    {
        return false;
    }
    return MatchesPublishTemplate(cluster, attribute);
}

bool PlatformDeviceMqttProfile::IsCommandAllowed(chip::EndpointId endpoint, chip::ClusterId cluster,
                                                 chip::CommandId command) const
{
    if (!IsEndpointAllowed(endpoint))
    {
        return false;
    }
#if defined(ZCL_USING_ON_OFF_CLUSTER_SERVER)
    if (cluster == chip::app::Clusters::OnOff::Id)
    {
        return command == chip::app::Clusters::OnOff::Commands::On::Id ||
            command == chip::app::Clusters::OnOff::Commands::Off::Id ||
            command == chip::app::Clusters::OnOff::Commands::Toggle::Id;
    }
#endif
#if defined(ZCL_USING_THERMOSTAT_CLUSTER_SERVER)
    if (cluster == chip::app::Clusters::Thermostat::Id)
    {
        return command == chip::app::Clusters::Thermostat::Commands::SetpointRaiseLower::Id;
    }
#endif
#if defined(ZCL_USING_DOOR_LOCK_CLUSTER_SERVER)
    if (cluster == chip::app::Clusters::DoorLock::Id)
    {
        if (command == chip::app::Clusters::DoorLock::Commands::UnlockDoor::Id)
        {
            return false;
        }
        return command == chip::app::Clusters::DoorLock::Commands::LockDoor::Id;
    }
#endif
#if defined(ZCL_USING_WINDOW_COVERING_CLUSTER_SERVER)
    if (cluster == chip::app::Clusters::WindowCovering::Id)
    {
        return command == chip::app::Clusters::WindowCovering::Commands::GoToLiftPercentage::Id ||
            command == chip::app::Clusters::WindowCovering::Commands::GoToTiltPercentage::Id;
    }
#endif
    (void) command;
    return false;
}

const MatterMqttSnapshotPath * PlatformDeviceMqttProfile::SnapshotPaths() const
{
    EnsureSnapshotPathsBuilt();
    return mSnapshotPathCount > 0 ? mSnapshotPaths : nullptr;
}

size_t PlatformDeviceMqttProfile::SnapshotPathCount() const
{
    EnsureSnapshotPathsBuilt();
    return mSnapshotPathCount;
}

CHIP_ERROR PlatformDeviceMqttProfile::RefreshMqttDeviceId()
{
    chip::NodeId nodeId = chip::kUndefinedNodeId;
    for (const chip::FabricInfo & fabric : chip::Server::GetInstance().GetFabricTable())
    {
        nodeId = fabric.GetNodeId();
        break;
    }
    VerifyOrReturnError(nodeId != chip::kUndefinedNodeId, CHIP_ERROR_INCORRECT_STATE);
    ReturnErrorOnFailure(FormatMqttDeviceId(nodeId, mMqttDeviceId, sizeof(mMqttDeviceId)));
    mMqttDeviceIdKnown = true;
    return CHIP_NO_ERROR;
}

const char * PlatformDeviceMqttProfile::GetMqttDeviceId() const
{
    return mMqttDeviceId;
}

bool PlatformDeviceMqttProfile::IsMqttDeviceIdKnown() const
{
    return mMqttDeviceIdKnown;
}

PlatformMqttBridgeConfig::PlatformMqttBridgeConfig() : mAdapters(GetPlatformMqttAdapters())
{
    profile  = &mProfile;
    adapters = &mAdapters;
}

PlatformMqttBridgeConfig gPlatformMqttBridgeConfig;
