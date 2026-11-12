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

#include "matter_mqtt_contract.h"

#include <lib/core/CHIPError.h>
#include <lib/core/DataModelTypes.h>
#include <lib/core/NodeId.h>

#include <cstddef>

struct MatterMqttSnapshotPath
{
    chip::EndpointId endpoint;
    chip::ClusterId clusterId;
    chip::AttributeId attributeId;
};

/** MQTT device id formatting (matter/v1/{deviceId}/...). */
CHIP_ERROR FormatMqttDeviceId(chip::NodeId nodeId, char * out, size_t outLen);

/**
 * Product ACL and MQTT topic device id. Applications supply a concrete subclass
 */
class DeviceMqttProfile
{
public:
    virtual ~DeviceMqttProfile() = default;

    /** True when @ref SnapshotPathCount is non-zero (bridge skips listener/snapshot otherwise). */
    bool SupportsAttributePublishing() const { return SnapshotPathCount() > 0; }

    virtual bool IsAttributeAllowed(chip::EndpointId endpoint, chip::ClusterId cluster,
                                    chip::AttributeId attribute) const;

    virtual bool IsCommandAllowed(chip::EndpointId endpoint, chip::ClusterId cluster, chip::CommandId command) const = 0;

    /** Checked on inbound commands after topic parse; endpoint policy is product-defined. */
    virtual bool IsEndpointAllowed(chip::EndpointId endpoint) const;

    virtual const MatterMqttSnapshotPath * SnapshotPaths() const { return nullptr; }
    virtual size_t SnapshotPathCount() const { return 0; }

    virtual CHIP_ERROR RefreshMqttDeviceId() = 0;
    virtual const char * GetMqttDeviceId() const           = 0;
    virtual bool IsMqttDeviceIdKnown() const               = 0;
};

/** Returns whether @p endpoint is an enabled entry from CodegenDataModelProvider::Endpoints(). */
bool MatterMqttIsEndpointInDataModel(chip::EndpointId endpoint);
