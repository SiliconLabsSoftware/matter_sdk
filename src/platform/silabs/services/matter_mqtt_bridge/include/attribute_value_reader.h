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

#include <app/ConcreteAttributePath.h>
#include <lib/core/CHIPError.h>

#include <cstddef>

/**
 * Reads Matter attribute values as MQTT v1 decimal/bool/null strings (Matter platform thread).
 * Product code supplies a concrete implementation via MatterMqttBridgeConfig::reader.
 */
class AttributeValueReader
{
public:
    virtual ~AttributeValueReader() = default;

    /** Writes MQTT wire text into @p out (NUL-terminated). */
    virtual CHIP_ERROR ReadAsString(const chip::app::ConcreteAttributePath & path, char * out, size_t outLen) = 0;
};
