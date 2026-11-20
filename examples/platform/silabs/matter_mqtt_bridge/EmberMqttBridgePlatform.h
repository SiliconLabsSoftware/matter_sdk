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

#include <AttributeValueReader.h>
#include <CommandInjector.h>
#include <MatterMqttCodec.h>

/** Codegen attribute accessor read path for MQTT outbound reports. */
class EmberMqttAttributeValueReader final : public AttributeValueReader
{
public:
    CHIP_ERROR ReadAsString(const chip::app::ConcreteAttributePath & path, char * out, size_t outLen) override;
};

/** Ember/codegen path: MQTT JSON command args to invoke TLV. */
class PlatformMqttCommandInjector final : public MqttCommandInjector
{
public:
    CHIP_ERROR BuildInvokeReader(chip::ClusterId clusterId, chip::CommandId commandId,
                                 const MatterMqttCodecTypes::ArgsView * args, uint8_t * tlvBuffer, size_t tlvBufferSize,
                                 chip::TLV::TLVReader & reader, size_t & tlvLen) const override;
};
