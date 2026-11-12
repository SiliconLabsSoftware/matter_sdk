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

#include "cluster_adapter.h"
#include "matter_mqtt_types.h"

#include <lib/core/CHIPError.h>

/** Applies an inbound MQTT command to the Matter data model (product supplies implementation). */
class CommandInjector
{
public:
    virtual ~CommandInjector() = default;

    virtual CHIP_ERROR Invoke(const MatterMqttBridge::InboundCommand & command, const MatterMqttCommandEntry & entry,
                              const MatterMqttCodecTypes::ArgsView * decodedArgs = nullptr) = 0;
};

/** Injects commands through CodegenDataModelProvider::InvokeCommand on the Matter thread. */
class MqttCommandInjector final : public CommandInjector
{
public:
    CHIP_ERROR Invoke(const MatterMqttBridge::InboundCommand & command, const MatterMqttCommandEntry & entry,
                      const MatterMqttCodecTypes::ArgsView * decodedArgs = nullptr) override;
};
