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

#include "cluster_adapter.h"

#include <lib/support/CodeUtils.h>

#include <cstdlib>
#include <cstring>

// ClusterAdapterRegistry: Matter path lookup and MQTT JSON arg packing for command invoke.

namespace {

const char * ArgValueForName(const MatterMqttCodecTypes::ArgsView & args, const char * name)
{
    for (size_t i = 0; i < args.count; ++i)
    {
        if (strcmp(args.items[i].name, name) == 0)
        {
            return args.items[i].value;
        }
    }
    return nullptr;
}

} // namespace

ClusterAdapterRegistry::ClusterAdapterRegistry(chip::Span<const ClusterAdapter> adapters) : mAdapters(adapters) {}

const ClusterAdapter * ClusterAdapterRegistry::FindByClusterId(chip::ClusterId id) const
{
    for (const ClusterAdapter & adapter : mAdapters)
    {
        if (adapter.clusterId == id)
        {
            return &adapter;
        }
    }
    return nullptr;
}

const MatterMqttAttributeEntry * ClusterAdapterRegistry::FindAttribute(const ClusterAdapter * adapter,
                                                                       chip::AttributeId id) const
{
    VerifyOrReturnError(adapter != nullptr, nullptr);
    for (size_t i = 0; i < adapter->attributeCount; ++i)
    {
        if (adapter->attributes[i].id == id)
        {
            return &adapter->attributes[i];
        }
    }
    return nullptr;
}

const MatterMqttCommandEntry * ClusterAdapterRegistry::FindCommand(const ClusterAdapter * adapter, chip::CommandId id) const
{
    VerifyOrReturnError(adapter != nullptr, nullptr);
    for (size_t i = 0; i < adapter->commandCount; ++i)
    {
        if (adapter->commands[i].id == id)
        {
            return &adapter->commands[i];
        }
    }
    return nullptr;
}

// JSON arg strings -> compact byte packing consumed by CommandInjector (UTF-8 fields read from ArgsView directly).
CHIP_ERROR ClusterAdapterRegistry::PackCommandArgs(const MatterMqttCommandEntry & entry,
                                                   const MatterMqttCodecTypes::ArgsView & args, uint8_t * out,
                                                   uint8_t & outLen) const
{
    VerifyOrReturnError(out != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    outLen = 0;

    size_t offset = 0;
    for (uint8_t i = 0; i < entry.argSchemaCount; ++i)
    {
        const MatterMqttArgField & field = entry.argSchema[i];
        const char * text                = ArgValueForName(args, field.name);

        switch (field.kind)
        {
        // UTF-8: not packed; CommandInjector reads JSON text from ArgsView by field name.
        case MatterMqttArgKind::kUtf8String:
            if (text == nullptr && field.optional)
            {
                continue;
            }
            VerifyOrReturnError(text != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
            continue;
        // Boolean: parse JSON "true"/"1" into one byte for BuildCommandReader.
        case MatterMqttArgKind::kBoolean: {
            VerifyOrReturnError(text != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
            VerifyOrReturnError(offset + 1 <= MATTER_MQTT_MAX_CMD_ARG_PACK, CHIP_ERROR_BUFFER_TOO_SMALL);
            out[offset++] = (strcmp(text, "true") == 0 || strcmp(text, "1") == 0) ? 1 : 0;
            break;
        }
        // Signed integer: parse JSON decimal into one int8-sized byte.
        case MatterMqttArgKind::kSignedInteger: {
            VerifyOrReturnError(text != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
            char * end          = nullptr;
            const long v        = strtol(text, &end, 10);
            VerifyOrReturnError(end != text && *end == '\0', CHIP_ERROR_INVALID_ARGUMENT);
            VerifyOrReturnError(offset + sizeof(int8_t) <= MATTER_MQTT_MAX_CMD_ARG_PACK, CHIP_ERROR_BUFFER_TOO_SMALL);
            out[offset++] = static_cast<uint8_t>(static_cast<int8_t>(v));
            break;
        }
        // Unsigned integer: parse JSON decimal into one byte.
        case MatterMqttArgKind::kUnsignedInteger: {
            VerifyOrReturnError(text != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
            char * end            = nullptr;
            const unsigned long v = strtoul(text, &end, 10);
            VerifyOrReturnError(end != text && *end == '\0', CHIP_ERROR_INVALID_ARGUMENT);
            VerifyOrReturnError(offset + sizeof(uint8_t) <= MATTER_MQTT_MAX_CMD_ARG_PACK, CHIP_ERROR_BUFFER_TOO_SMALL);
            out[offset++] = static_cast<uint8_t>(v);
            break;
        }
        default:
            return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
        }
    }
    outLen = static_cast<uint8_t>(offset);
    return CHIP_NO_ERROR;
}