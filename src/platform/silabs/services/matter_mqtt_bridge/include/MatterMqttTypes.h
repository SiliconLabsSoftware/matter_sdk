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

#include <lib/core/DataModelTypes.h>
#include <platform/silabs/services/mqtt_client.h>

#include <cstddef>
#include <cstdint>

/** POD types exchanged across the Matter MQTT bridge threads. */
namespace MatterMqttBridge {

enum class RawValueType : uint8_t
{
    kNull     = 0,
    kBool     = 1,
    kSigned   = 2,
    kUnsigned = 3,
};

struct RawValue
{
    RawValueType type = RawValueType::kNull;
    union
    {
        bool boolean;
        int32_t signedValue;
        uint32_t unsignedValue;
    } data{};
};

/**
 * MQTT-thread work payload for attribute reports. NodeId is omitted: attribute/command topics use
 * DeviceIdSource when building/parsing MQTT topics (bind time), not when queuing this struct.
 */
struct ReportWork
{
    chip::EndpointId endpoint     = 0;
    chip::ClusterId clusterId     = 0;
    chip::AttributeId attributeId   = 0;
    RawValueType valueType          = RawValueType::kNull;
    union
    {
        uint8_t boolean;
        int32_t signedValue;
        uint32_t unsignedValue;
    } value{};
};

static_assert(sizeof(ReportWork) <= chip::DeviceLayer::Silabs::MqttClient::kWorkDataSize,
              "ReportWork must fit MqttClient::ScheduleWork payload");

/**
 * Matter-thread command inject payload. Queued via a static slot in MatterMqttBridge (not ScheduleLambda memcpy).
 * device NodeId comes from the inbound MQTT topic via DeviceIdSource, not stored here.
 */
struct InboundCommand
{
    chip::EndpointId endpoint   = 0;
    chip::ClusterId clusterId   = 0;
    chip::CommandId commandId   = 0;
    uint8_t args[MATTER_MQTT_MAX_CMD_ARG_PACK]{};
    uint8_t argsLen             = 0;
};

enum CommandFlags : uint8_t
{
    kCommandFlagNone         = 0x00,
    kCommandFlagTimed        = 0x01, /**< Timed invoke required (e.g. Door Lock PIN). */
    kCommandFlagFabricScoped = 0x02,
    kCommandFlagHasResponse  = 0x04,
    kCommandFlagUnsupported  = 0x08,
};

} // namespace MatterMqttBridge
