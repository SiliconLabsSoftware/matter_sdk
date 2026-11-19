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

#include "MatterMqttBridgeConfigImpl.h"

#include <MatterMqttLog.h>
#include <zap-generated/gen_config.h>

#include <app/util/attribute-storage.h>
#include <data-model-providers/codegen/CodegenDataModelProvider.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/ReadOnlyBuffer.h>
#include <lib/support/logging/CHIPLogging.h>
#include <server/Server.h>

PlatformDeviceMqttProfile::PlatformDeviceMqttProfile(const MatterMqttClusterFilterRegistry * filterRegistry)
    : mFilterRegistry(filterRegistry)
{}

void PlatformDeviceMqttProfile::BuildSnapshotPaths()
{
    if (mSnapshotPathsBuilt || mFilterRegistry == nullptr)
    {
        mSnapshotPathsBuilt = true;
        return;
    }

    mSnapshotPathCount = 0;

    chip::ReadOnlyBufferBuilder<chip::app::DataModel::EndpointEntry> endpoints;
    if (chip::app::CodegenDataModelProvider::Instance().Endpoints(endpoints) != CHIP_NO_ERROR)
    {
        MATTER_MQTT_LOG_ERROR("snapshot path build: Endpoints() failed");
        mSnapshotPathsBuilt = true;
        return;
    }

    for (const chip::app::DataModel::EndpointEntry & endpoint : endpoints.TakeBuffer())
    {
        for (const MatterMqttClusterFilter & filter : mFilterRegistry->Filters())
        {
            if (!emberAfContainsServer(endpoint.id, filter.clusterId))
            {
                continue;
            }
            for (size_t a = 0; a < filter.attributeCount; ++a)
            {
                if (mSnapshotPathCount >= MATTER_MQTT_MAX_SNAPSHOT_PATHS)
                {
                    mSnapshotPathsBuilt = true;
                    return;
                }
                mSnapshotPaths[mSnapshotPathCount].endpoint    = endpoint.id;
                mSnapshotPaths[mSnapshotPathCount].clusterId   = filter.clusterId;
                mSnapshotPaths[mSnapshotPathCount].attributeId = filter.attributes[a].id;
                ++mSnapshotPathCount;
            }
        }
    }
    MATTER_MQTT_LOG("snapshot paths built: %u", static_cast<unsigned>(mSnapshotPathCount));
    mSnapshotPathsBuilt = true;
}

bool PlatformDeviceMqttProfile::IsAttributeAllowed(chip::EndpointId endpoint, chip::ClusterId cluster,
                                                   chip::AttributeId attribute) const
{
    if (!IsEndpointAllowed(endpoint) || mFilterRegistry == nullptr)
    {
        return false;
    }
    const MatterMqttClusterFilter * clusterFilter = mFilterRegistry->FindByClusterId(cluster);
    return clusterFilter != nullptr && mFilterRegistry->FindAttribute(clusterFilter, attribute) != nullptr;
}

const MatterMqttSnapshotPath * PlatformDeviceMqttProfile::SnapshotPaths() const
{
    return mSnapshotPathCount > 0 ? mSnapshotPaths : nullptr;
}

size_t PlatformDeviceMqttProfile::SnapshotPathCount() const
{
    return mSnapshotPathCount;
}

CHIP_ERROR PlatformDeviceMqttProfile::ResolveMqttDeviceNodeId()
{
    chip::NodeId nodeId = chip::kUndefinedNodeId;
    for (const chip::FabricInfo & fabric : chip::Server::GetInstance().GetFabricTable())
    {
        nodeId = fabric.GetNodeId();
        break;
    }
    VerifyOrReturnError(nodeId != chip::kUndefinedNodeId, CHIP_ERROR_INCORRECT_STATE);
    mMqttDeviceNodeId  = nodeId;
    mMqttDeviceIdKnown = true;
    BuildSnapshotPaths();
    MATTER_MQTT_LOG("MQTT device id ready: 0x" ChipLogFormatX64, ChipLogValueX64(mMqttDeviceNodeId));
    return CHIP_NO_ERROR;
}

chip::NodeId PlatformDeviceMqttProfile::GetMqttDeviceNodeId() const
{
    return mMqttDeviceNodeId;
}

bool PlatformDeviceMqttProfile::IsMqttDeviceIdKnown() const
{
    return mMqttDeviceIdKnown;
}

PlatformMqttBridgeConfig::PlatformMqttBridgeConfig()
    : mFilterRegistry(GetPlatformMqttClusterFilters()), mProfile(&mFilterRegistry)
{
    profile        = &mProfile;
    filterRegistry = &mFilterRegistry;
    injector       = &mCommandInjector;
    reader         = &mAttributeReader;
}

PlatformMqttBridgeConfig gPlatformMqttBridgeConfig;
