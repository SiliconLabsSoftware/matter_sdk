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
#include <lib/support/Span.h>

#include <cstddef>
#include <cstdint>

/** Decoded command argument name/value pairs from an inbound MQTT JSON payload. */
namespace MatterMqttCodecTypes {

struct NameValue
{
    const char * name;
    const char * value;
};

struct ArgsView
{
    NameValue items[MATTER_MQTT_MAX_ARGS];
    size_t count;
};

} // namespace MatterMqttCodecTypes

/** Encodes attribute reports and decodes command payloads for the MQTT contract. */
class MatterMqttCodec
{
public:
    virtual ~MatterMqttCodec() = default;

    virtual CHIP_ERROR EncodeReport(bool asNumber, chip::CharSpan valueText, char * outJson, size_t outLen) = 0;

    virtual CHIP_ERROR DecodeArgs(chip::ByteSpan json, MatterMqttCodecTypes::ArgsView & out) = 0;
};

class MatterMqttJsonCodec final : public MatterMqttCodec
{
public:
    CHIP_ERROR EncodeReport(bool asNumber, chip::CharSpan valueText, char * outJson, size_t outLen) override;

    CHIP_ERROR DecodeArgs(chip::ByteSpan json, MatterMqttCodecTypes::ArgsView & out) override;
};
