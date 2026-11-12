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

#include "matter_mqtt_types.h"

#include <app/ConcreteAttributePath.h>
#include <lib/core/CHIPError.h>

/**
 * Reads Matter attribute values for MQTT outbound reports (Matter platform thread).
 *
 * The abstract base exists for dependency injection and host tests, not because Ember and code-driven
 * clusters use different reader subclasses: @ref CodeDrivenAttributeReader handles both via
 * EncodeReadBestEffort (registry ReadAttribute first, else CodegenDataModelProvider::ReadAttribute).
 */
class AttributeValueReader
{
public:
    virtual ~AttributeValueReader() = default;

    /** Fills @p out with a primitive scalar read from @p path. */
    virtual CHIP_ERROR Read(const chip::app::ConcreteAttributePath & path, MatterMqttBridge::RawValue & out) = 0;
};

/**
 * Uses CodegenDataModelProvider: ServerClusterInterface::ReadAttribute when the cluster
 * is in the code-driven registry, otherwise CodegenDataModelProvider::ReadAttribute.
 */
class CodeDrivenAttributeReader final : public AttributeValueReader
{
public:
    CHIP_ERROR Read(const chip::app::ConcreteAttributePath & path, MatterMqttBridge::RawValue & out) override;
};
