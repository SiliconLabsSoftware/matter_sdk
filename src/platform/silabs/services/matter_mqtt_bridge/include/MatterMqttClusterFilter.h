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

#include <lib/core/DataModelTypes.h>
#include <lib/support/Span.h>

#include <cstddef>
#include <cstdint>

class DeviceMqttProfile;

namespace MatterMqttTopic {
    struct ParsedCommandTopic;
}

/** Product-specific MQTT allow-lists for Matter attribute reports and commands. */

struct MatterMqttAttributeFilter
{
    chip::AttributeId id;
};

struct MatterMqttCommandFilter
{
    chip::CommandId id;
};

struct MatterMqttClusterFilter
{
    chip::ClusterId clusterId;
    const MatterMqttAttributeFilter * attributes;
    size_t attributeCount;
    const MatterMqttCommandFilter * commands;
    size_t commandCount;
};

/** Lookup for cluster attribute/command allow-lists. */
class MatterMqttClusterFilterRegistry
{
public:
    explicit MatterMqttClusterFilterRegistry(chip::Span<const MatterMqttClusterFilter> filters);

    const MatterMqttClusterFilter * FindByClusterId(chip::ClusterId id) const;
    const MatterMqttAttributeFilter * FindAttribute(const MatterMqttClusterFilter * filter, chip::AttributeId id) const;
    const MatterMqttCommandFilter * FindCommand(const MatterMqttClusterFilter * filter, chip::CommandId id) const;

    chip::Span<const MatterMqttClusterFilter> Filters() const { return mFilters; }

private:
    chip::Span<const MatterMqttClusterFilter> mFilters;
};

/**
 * Command allow-list for a parsed topic: endpoint, cluster, attributes on that endpoint, and command registry lookup.
 * Logs `command rejected ... (reason)` and returns false on failure. Node-id match is the caller's job.
 */
bool ValidateParsedCommand(const DeviceMqttProfile & profile, const MatterMqttClusterFilterRegistry * registry,
                           const MatterMqttTopic::ParsedCommandTopic & parsed);
