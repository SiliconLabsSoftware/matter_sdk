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

#include "MatterMqttCodec.h"
#include "MatterMqttTypes.h"

#include <lib/core/DataModelTypes.h>
#include <lib/core/TLV.h>
#include <lib/support/Span.h>

#include <cstddef>
#include <cstdint>

/** Product-specific mapping between MQTT cluster/command names and Matter data model paths. */

enum class MatterMqttAttributeQuantity : uint8_t
{
    kScalar, // Boolean MQTT strings "on"/"off" (e.g. OnOff::OnOff)
    kTemperatureDeciDegrees,
    kPercent100ths,
    kEnum, // Enum name via ClusterAdapter::formatEnum
};

struct MatterMqttAttributeEntry
{
    const char * name;
    chip::AttributeId id;
    MatterMqttAttributeQuantity quantity;
};

struct MatterMqttArgField
{
    const char * name;
    uint8_t fieldId;
    chip::TLV::TLVType tlvType;
    bool optional = false;
};

struct MatterMqttCommandEntry
{
    const char * name;
    const char * const * aliases;
    chip::CommandId id;
    MatterMqttBridge::CommandFlags flags;
    const MatterMqttArgField * argSchema;
    uint8_t argSchemaCount;
};

struct ClusterAdapter
{
    chip::ClusterId clusterId;
    const char * clusterName;
    const MatterMqttAttributeEntry * attributes;
    size_t attributeCount;
    const MatterMqttCommandEntry * commands;
    size_t commandCount;
    const char * (*formatEnum)(chip::AttributeId attrId, uint32_t raw);
};

class ClusterAdapterRegistry
{
public:
    virtual ~ClusterAdapterRegistry() = default;

    virtual const ClusterAdapter * FindByClusterId(chip::ClusterId id) const  = 0;
    virtual const ClusterAdapter * FindByClusterName(chip::CharSpan name) const = 0;
    virtual bool ResolveAttributeId(const ClusterAdapter * adapter, chip::CharSpan name, chip::AttributeId & out) const = 0;
    virtual bool ResolveCommandId(const ClusterAdapter * adapter, chip::CharSpan name, chip::CommandId & out,
                                  const MatterMqttCommandEntry ** outEntry) const                                   = 0;
    virtual const char * AttributeNameOf(const ClusterAdapter * adapter, chip::AttributeId id) const                = 0;
    virtual const MatterMqttAttributeEntry * FindAttribute(const ClusterAdapter * adapter, chip::AttributeId id) const = 0;
    virtual const MatterMqttCommandEntry * FindCommand(const ClusterAdapter * adapter, chip::CommandId id) const       = 0;
    virtual CHIP_ERROR PackCommandArgs(const MatterMqttCommandEntry & entry, const MatterMqttCodecTypes::ArgsView & args,
                                       uint8_t * out, uint8_t & outLen) const                                        = 0;
};

class StaticClusterAdapterRegistry final : public ClusterAdapterRegistry
{
public:
    StaticClusterAdapterRegistry(chip::Span<const ClusterAdapter> adapters);
    const ClusterAdapter * FindByClusterId(chip::ClusterId id) const override;
    const ClusterAdapter * FindByClusterName(chip::CharSpan name) const override;
    bool ResolveAttributeId(const ClusterAdapter * adapter, chip::CharSpan name, chip::AttributeId & out) const override;
    bool ResolveCommandId(const ClusterAdapter * adapter, chip::CharSpan name, chip::CommandId & out,
                          const MatterMqttCommandEntry ** outEntry) const override;
    const char * AttributeNameOf(const ClusterAdapter * adapter, chip::AttributeId id) const override;
    const MatterMqttAttributeEntry * FindAttribute(const ClusterAdapter * adapter, chip::AttributeId id) const override;
    const MatterMqttCommandEntry * FindCommand(const ClusterAdapter * adapter, chip::CommandId id) const override;
    CHIP_ERROR PackCommandArgs(const MatterMqttCommandEntry & entry, const MatterMqttCodecTypes::ArgsView & args, uint8_t * out,
                               uint8_t & outLen) const override;

private:
    chip::Span<const ClusterAdapter> mAdapters;
};

