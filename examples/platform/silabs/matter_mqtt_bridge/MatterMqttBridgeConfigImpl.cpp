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

#include <lib/support/CodeUtils.h>
#include <server/Server.h>

PlatformDeviceMqttProfile::PlatformDeviceMqttProfile(const MatterMqttClusterFilterRegistry * filterRegistry)
    : mFilterRegistry(filterRegistry)
{}

bool PlatformDeviceMqttProfile::IsAttributeAllowed(chip::ClusterId cluster,
                                                   chip::AttributeId attribute) const
{
    if (mFilterRegistry == nullptr)
    {
        return false;
    }
    const MatterMqttClusterFilter * clusterFilter = mFilterRegistry->FindByClusterId(cluster);
    return clusterFilter != nullptr && mFilterRegistry->FindAttribute(clusterFilter, attribute) != nullptr;
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
