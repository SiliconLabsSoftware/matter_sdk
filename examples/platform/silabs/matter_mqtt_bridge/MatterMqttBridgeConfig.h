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

#include <cluster_adapter.h>
#include <device_mqtt_profile.h>
#include <lib/support/Span.h>
#include <matter_mqtt_bridge_config.h>

/** Matter cluster/attribute pair allowed for MQTT publish (endpoints resolved from the data model). */
struct MatterMqttPublishTemplate
{
    chip::ClusterId clusterId;
    chip::AttributeId attributeId;
};

class PlatformDeviceMqttProfile final : public DeviceMqttProfile
{
public:
    bool IsAttributeAllowed(chip::EndpointId endpoint, chip::ClusterId cluster, chip::AttributeId attribute) const override;
    bool IsCommandAllowed(chip::EndpointId endpoint, chip::ClusterId cluster, chip::CommandId command) const override;

    const MatterMqttSnapshotPath * SnapshotPaths() const override;
    size_t SnapshotPathCount() const override;

    CHIP_ERROR RefreshMqttDeviceId() override;
    const char * GetMqttDeviceId() const override;
    bool IsMqttDeviceIdKnown() const override;

private:
    static bool MatchesPublishTemplate(chip::ClusterId cluster, chip::AttributeId attribute);

    void EnsureSnapshotPathsBuilt() const;

    char mMqttDeviceId[MATTER_MQTT_DEVICE_ID_LEN] = {};
    bool mMqttDeviceIdKnown                       = false;
    mutable MatterMqttSnapshotPath mSnapshotPaths[MATTER_MQTT_MAX_SNAPSHOT_PATHS]{};
    mutable size_t mSnapshotPathCount = 0;
    mutable bool mSnapshotPathsBuilt  = false;
};

class PlatformMqttBridgeConfig final : public MatterMqttBridgeConfig
{
public:
    PlatformMqttBridgeConfig();

private:
    PlatformDeviceMqttProfile mProfile;
    ClusterAdapterRegistry mAdapters;
};

extern PlatformMqttBridgeConfig gPlatformMqttBridgeConfig;

chip::Span<const ClusterAdapter> GetPlatformMqttAdapters();
