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
#include <lib/support/Span.h>

#include <cstddef>

/**
 * Builds and parses MQTT topics for the Matter MQTT bridge v1 contract.
 * Path ids use uppercase hex without a "0x" prefix or leading zeros.
 */
namespace MatterMqttTopic {

/** Formats one numeric id for a topic segment (uppercase hex, no 0x, no leading zeros). */
CHIP_ERROR FormatTopicIdUint64(uint64_t value, char * out, size_t outLen);

CHIP_ERROR BuildAttributeTopic(chip::NodeId deviceNodeId, chip::EndpointId ep, chip::ClusterId clusterId,
                               chip::AttributeId attributeId, char * out, size_t outLen);

CHIP_ERROR BuildCommandFilter(chip::NodeId deviceNodeId, char * out, size_t outLen);

struct ParsedCommandTopic
{
    chip::EndpointId endpoint  = 0;
    chip::ClusterId clusterId    = 0;
    chip::CommandId commandId    = 0;
};

/** Validates @p topic against @p expectedDeviceNodeId and fills @p out with command path fields. */
CHIP_ERROR ParseCommandTopic(chip::CharSpan topic, chip::NodeId expectedDeviceNodeId, ParsedCommandTopic & out);

} // namespace MatterMqttTopic