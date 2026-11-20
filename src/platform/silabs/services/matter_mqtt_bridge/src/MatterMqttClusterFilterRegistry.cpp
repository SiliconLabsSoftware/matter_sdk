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

#include "MatterMqttClusterFilter.h"
#include "DeviceMqttProfile.h"
#include "MatterMqttLog.h"
#include "MatterMqttTopic.h"

#include <app/util/attribute-storage.h>
#include <app/util/endpoint-config-api.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>

MatterMqttClusterFilterRegistry::MatterMqttClusterFilterRegistry(chip::Span<const MatterMqttClusterFilter> filters)
    : mFilters(filters)
{}

const MatterMqttClusterFilter * MatterMqttClusterFilterRegistry::FindByClusterId(chip::ClusterId id) const
{
    for (const MatterMqttClusterFilter & filter : mFilters)
    {
        if (filter.clusterId == id)
        {
            return &filter;
        }
    }
    return nullptr;
}

const MatterMqttAttributeFilter * MatterMqttClusterFilterRegistry::FindAttribute(const MatterMqttClusterFilter * filter,
                                                                                 chip::AttributeId id) const
{
    VerifyOrReturnError(filter != nullptr, nullptr);
    for (size_t i = 0; i < filter->attributeCount; ++i)
    {
        if (filter->attributes[i].id == id)
        {
            return &filter->attributes[i];
        }
    }
    return nullptr;
}

const MatterMqttCommandFilter * MatterMqttClusterFilterRegistry::FindCommand(const MatterMqttClusterFilter * filter,
                                                                             chip::CommandId id) const
{
    VerifyOrReturnError(filter != nullptr, nullptr);
    for (size_t i = 0; i < filter->commandCount; ++i)
    {
        if (filter->commands[i].id == id)
        {
            return &filter->commands[i];
        }
    }
    return nullptr;
}

bool ValidateParsedCommand(const DeviceMqttProfile & profile, const MatterMqttClusterFilterRegistry * registry,
                           const MatterMqttTopic::ParsedCommandTopic & parsed)
{
    if (registry == nullptr)
    {
        MATTER_MQTT_LOG_ERROR("command rejected cmd=" ChipLogFormatMEI " (no_registry)", ChipLogValueMEI(parsed.commandId));
        return false;
    }

    if (!profile.IsEndpointAllowed(parsed.endpoint))
    {
        MATTER_MQTT_LOG_ERROR("command rejected cmd=" ChipLogFormatMEI " (endpoint)", ChipLogValueMEI(parsed.commandId));
        return false;
    }

    const MatterMqttClusterFilter * clusterFilter = registry->FindByClusterId(parsed.clusterId);
    if (clusterFilter == nullptr)
    {
        MATTER_MQTT_LOG_ERROR("command rejected cmd=" ChipLogFormatMEI " (unknown_cluster)", ChipLogValueMEI(parsed.commandId));
        return false;
    }

    // The filter is global. Confirm its attributes are present on this endpoint.
    bool attributesOnEndpoint = true;
    if (clusterFilter->attributeCount == 0)
    {
        attributesOnEndpoint = emberAfContainsServer(parsed.endpoint, parsed.clusterId);
    }
    else
    {
        for (size_t i = 0; i < clusterFilter->attributeCount; ++i)
        {
            if (!emberAfContainsAttribute(parsed.endpoint, parsed.clusterId, clusterFilter->attributes[i].id))
            {
                attributesOnEndpoint = false;
                break;
            }
        }
    }
    if (!attributesOnEndpoint)
    {
        MATTER_MQTT_LOG_ERROR("command rejected cmd=" ChipLogFormatMEI " (denied)", ChipLogValueMEI(parsed.commandId));
        return false;
    }

    if (registry->FindCommand(clusterFilter, parsed.commandId) == nullptr)
    {
        MATTER_MQTT_LOG_ERROR("command rejected cmd=" ChipLogFormatMEI " (unknown_command)", ChipLogValueMEI(parsed.commandId));
        return false;
    }

    return true;
}
