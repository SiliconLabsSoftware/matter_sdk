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

#include "matter_mqtt_cluster_filter.h"

#include <lib/support/CodeUtils.h>

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

const MatterMqttFilterAttributeEntry * MatterMqttClusterFilterRegistry::FindAttribute(const MatterMqttClusterFilter * filter,
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

const MatterMqttFilterCommandEntry * MatterMqttClusterFilterRegistry::FindCommand(const MatterMqttClusterFilter * filter,
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
