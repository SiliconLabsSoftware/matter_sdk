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
#include "MatterMqttTopic.h"

#include <lib/core/CHIPError.h>
#include <lib/core/DataModelTypes.h>
#include <lib/core/TLV.h>

/**
 * JSON command args to invoke TLV, then CodegenDataModelProvider::InvokeCommand (Matter thread).
 * Product code subclasses and implements BuildInvokeReader.
 */
class MqttCommandInjector
{
public:
    virtual ~MqttCommandInjector() = default;

    /** MQTT thread: validate JSON args and write invoke-structure TLV into tlvBuffer. */
    CHIP_ERROR EncodeInvokeTlv(chip::ClusterId clusterId, chip::CommandId commandId,
                               const MatterMqttCodecTypes::ArgsView * args, uint8_t * tlvBuffer, size_t tlvBufferSize,
                               size_t & tlvLen) const;

    virtual CHIP_ERROR BuildInvokeReader(chip::ClusterId clusterId, chip::CommandId commandId,
                                         const MatterMqttCodecTypes::ArgsView * args, uint8_t * tlvBuffer, size_t tlvBufferSize,
                                         chip::TLV::TLVReader & reader, size_t & tlvLen) const = 0;

    CHIP_ERROR Invoke(const MatterMqttTopic::ParsedCommandTopic & command, const uint8_t * invokeTlv, size_t invokeTlvLen);
};