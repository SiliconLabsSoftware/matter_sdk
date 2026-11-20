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

#include "EmberMqttBridgePlatform.h"

#include <MatterMqttClusterFilter.h>
#include <DeviceMqttProfile.h>
#include <lib/support/Span.h>
#include <MatterMqttBridgeConfig.h>

class PlatformDeviceMqttProfile final : public DeviceMqttProfile
{
public:
    explicit PlatformDeviceMqttProfile(const MatterMqttClusterFilterRegistry * filterRegistry);

    bool IsAttributeAllowed(chip::ClusterId cluster, chip::AttributeId attribute) const override;

    CHIP_ERROR ResolveMqttDeviceNodeId() override;
    chip::NodeId GetMqttDeviceNodeId() const override;
    bool IsMqttDeviceIdKnown() const override;

private:
    const MatterMqttClusterFilterRegistry * mFilterRegistry;
    chip::NodeId mMqttDeviceNodeId = chip::kUndefinedNodeId;
    bool mMqttDeviceIdKnown        = false;
};

class PlatformMqttBridgeConfig final : public MatterMqttBridgeConfig
{
public:
    PlatformMqttBridgeConfig();

private:
    MatterMqttClusterFilterRegistry mFilterRegistry;
    PlatformDeviceMqttProfile mProfile;
    PlatformMqttCommandInjector mCommandInjector;
    EmberMqttAttributeValueReader mAttributeReader;
};

extern PlatformMqttBridgeConfig gPlatformMqttBridgeConfig;

chip::Span<const MatterMqttClusterFilter> GetPlatformMqttClusterFilters();
