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

// ClusterAdapterRegistry: name/id resolution and MQTT JSON arg packing for command invoke.

namespace {

bool SpanEqualsCString(chip::CharSpan span, const char * cstr)
{
    if (cstr == nullptr)
    {
        return false;
    }
    const size_t len = strlen(cstr);
    return span.size() == len && memcmp(span.data(), cstr, len) == 0;
}

bool NameMatches(const MatterMqttCommandEntry & entry, chip::CharSpan name)
{
    if (SpanEqualsCString(name, entry.name))
    {
        return true;
    }
    if (entry.aliases != nullptr)
    {
        for (size_t i = 0; entry.aliases[i] != nullptr; ++i)
        {
            if (SpanEqualsCString(name, entry.aliases[i]))
            {
                return true;
            }
        }
    }
    return false;
}

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

const ClusterAdapter * ClusterAdapterRegistry::FindByClusterName(chip::CharSpan name) const
{
    VerifyOrReturnError(!name.empty(), nullptr);
    for (const ClusterAdapter & adapter : mAdapters)
    {
        if (SpanEqualsCString(name, adapter.clusterName))
        {
            return &adapter;
        }
    }
    return nullptr;
}

bool ClusterAdapterRegistry::ResolveAttributeId(const ClusterAdapter * adapter, chip::CharSpan name,
                                                      chip::AttributeId & out) const
{
    VerifyOrReturnError(adapter != nullptr && !name.empty(), false);
    for (size_t i = 0; i < adapter->attributeCount; ++i)
    {
        if (SpanEqualsCString(name, adapter->attributes[i].name))
        {
            out = adapter->attributes[i].id;
            return true;
        }
    }
    return false;
}

bool ClusterAdapterRegistry::ResolveCommandId(const ClusterAdapter * adapter, chip::CharSpan name, chip::CommandId & out,
                                                    const MatterMqttCommandEntry ** outEntry) const
{
    VerifyOrReturnError(adapter != nullptr && !name.empty(), false);
    for (size_t i = 0; i < adapter->commandCount; ++i)
    {
        if (NameMatches(adapter->commands[i], name))
        {
            out = adapter->commands[i].id;
            if (outEntry != nullptr)
            {
                *outEntry = &adapter->commands[i];
            }
            return true;
        }
    }
    return false;
}

const char * ClusterAdapterRegistry::AttributeNameOf(const ClusterAdapter * adapter, chip::AttributeId id) const
{
    const MatterMqttAttributeEntry * entry = FindAttribute(adapter, id);
    return entry != nullptr ? entry->name : nullptr;
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

const MatterMqttCommandEntry * ClusterAdapterRegistry::FindCommand(const ClusterAdapter * adapter,
                                                                         chip::CommandId id) const
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

        switch (field.tlvType)
        {
        // Strings are not packed; CommandInjector reads them from ArgsView by name.
        case chip::TLV::TLVType::kTLVType_UTF8String:
            if (text == nullptr && field.optional)
            {
                continue;
            }
            VerifyOrReturnError(text != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
            continue;
        // Numeric/bool fields: parse JSON text to bytes in field order for BuildCommandReader.
        case chip::TLV::TLVType::kTLVType_Boolean: {
            VerifyOrReturnError(text != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
            VerifyOrReturnError(offset + 1 <= MATTER_MQTT_MAX_CMD_ARG_PACK, CHIP_ERROR_BUFFER_TOO_SMALL);
            out[offset++] = (strcmp(text, "true") == 0 || strcmp(text, "1") == 0) ? 1 : 0;
            break;
        }
        case chip::TLV::TLVType::kTLVType_SignedInteger: {
            VerifyOrReturnError(text != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
            char * end          = nullptr;
            const long v        = strtol(text, &end, 10);
            VerifyOrReturnError(end != text && *end == '\0', CHIP_ERROR_INVALID_ARGUMENT);
            VerifyOrReturnError(offset + sizeof(int8_t) <= MATTER_MQTT_MAX_CMD_ARG_PACK, CHIP_ERROR_BUFFER_TOO_SMALL);
            out[offset++] = static_cast<uint8_t>(static_cast<int8_t>(v));
            break;
        }
        case chip::TLV::TLVType::kTLVType_UnsignedInteger: {
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
