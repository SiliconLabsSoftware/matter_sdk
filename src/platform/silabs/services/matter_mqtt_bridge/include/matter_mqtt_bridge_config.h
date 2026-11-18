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

#include "attribute_value_reader.h"
#include "command_injector.h"
#include "device_mqtt_profile.h"
#include "matter_mqtt_cluster_filter.h"
#include "mqtt_transport_port.h"

#include <cstdint>

/**
 * Dependency injection bundle for MatterMqttBridge::Init. Null @p transport selects the built-in port.
 * @p profile must be a non-null DeviceMqttProfile subclass. JSON codec is fixed (MatterMqttJsonCodec) in the bridge.
 */
class MatterMqttBridgeConfig
{
public:
    virtual ~MatterMqttBridgeConfig() = default;

    MqttTransportPort * transport                    = nullptr;
    MatterMqttClusterFilterRegistry * filterRegistry = nullptr;
    DeviceMqttProfile * profile                      = nullptr;
    AttributeValueReader * reader                    = nullptr;
    MqttCommandInjector * injector                   = nullptr;
    uint8_t commandQos                               = 0;
    uint8_t reportQos                                = 0;
    bool reportRetained                              = false;
};
