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

#include "device_mqtt_profile.h"

#include "matter_mqtt_topic.h"

#include <app/data-model-provider/Provider.h>
#include <data-model-providers/codegen/CodegenDataModelProvider.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/ReadOnlyBuffer.h> // IWYU pragma: keep

#include <cinttypes>
#include <cstdio>

bool DeviceMqttProfile::IsAttributeAllowed(chip::EndpointId endpoint, chip::ClusterId cluster, chip::AttributeId attribute) const
{
    (void) endpoint;
    (void) cluster;
    (void) attribute;
    return false;
}

bool DeviceMqttProfile::IsEndpointAllowed(chip::EndpointId endpoint) const
{
    return MatterMqttIsEndpointInDataModel(endpoint);
}

bool MatterMqttIsEndpointInDataModel(chip::EndpointId endpoint)
{
    chip::ReadOnlyBufferBuilder<chip::app::DataModel::EndpointEntry> builder;
    if (chip::app::CodegenDataModelProvider::Instance().Endpoints(builder) != CHIP_NO_ERROR)
    {
        return false;
    }
    for (const chip::app::DataModel::EndpointEntry & entry : builder.TakeBuffer())
    {
        if (entry.id == endpoint)
        {
            return true;
        }
    }
    return false;
}

// MQTT device id: 64-bit NodeId as uppercase hex without leading zeros (matches cluster/attr/cmd topic ids).
CHIP_ERROR FormatMqttDeviceId(chip::NodeId nodeId, char * out, size_t outLen)
{
    VerifyOrReturnError(out != nullptr && outLen >= MATTER_MQTT_DEVICE_ID_LEN, CHIP_ERROR_BUFFER_TOO_SMALL);

    return MatterMqttTopic::FormatTopicIdUint64(static_cast<uint64_t>(nodeId), out, outLen);
}
