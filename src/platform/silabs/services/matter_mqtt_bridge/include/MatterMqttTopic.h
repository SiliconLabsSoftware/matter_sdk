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
#include <lib/support/Span.h>

#include <cstddef>

/**
 * Builds and parses MQTT topics for the Matter MQTT bridge v1 contract
 * (matter/v1/{deviceId}/ep/...).
 */
namespace MatterMqttTopic {

CHIP_ERROR BuildAttributeTopic(chip::CharSpan deviceId, chip::EndpointId ep, chip::CharSpan clusterName, chip::CharSpan attrName,
                               char * out, size_t outLen);

CHIP_ERROR BuildCommandFilter(chip::CharSpan deviceId, bool useWildcard, char * out, size_t outLen);

struct ParsedCommandTopic
{
    chip::EndpointId endpoint   = 0;
    const char * clusterName    = nullptr;
    const char * commandName    = nullptr;
};

/** Validates @p topic against @p expectedDeviceId and fills @p out with command path fields. */
CHIP_ERROR ParseCommandTopic(chip::CharSpan topic, chip::CharSpan expectedDeviceId, ParsedCommandTopic & out);

} // namespace MatterMqttTopic
