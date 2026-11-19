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

#include "MatterMqttContract.h"

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

/**
 * Product ACL and MQTT topic device id. Applications supply a concrete subclass
 */
class DeviceMqttProfile
{
public:
    virtual ~DeviceMqttProfile() = default;

    /** Applies product attribute allow-list; must include endpoint policy (e.g. @ref IsEndpointAllowed). */
    virtual bool IsAttributeAllowed(chip::EndpointId endpoint, chip::ClusterId cluster,
                                    chip::AttributeId attribute) const;

    /**
     * Endpoint policy for attributes and command validation.
     * Reads the data model; call on the Matter thread or under chip::DeviceLayer::StackLock.
     */
    virtual bool IsEndpointAllowed(chip::EndpointId endpoint) const;

    virtual const MatterMqttSnapshotPath * SnapshotPaths() const { return nullptr; }
    virtual size_t SnapshotPathCount() const { return 0; }

    /** Resolves fabric NodeId for MQTT topics; safe to call until a fabric is commissioned. */
    virtual CHIP_ERROR ResolveMqttDeviceNodeId() = 0;
    virtual chip::NodeId GetMqttDeviceNodeId() const = 0;
    virtual bool IsMqttDeviceIdKnown() const         = 0;
};

/** Reads CodegenDataModelProvider::Endpoints(); use under chip::DeviceLayer::StackLock off the Matter thread. */
bool MatterMqttIsEndpointInDataModel(chip::EndpointId endpoint);
