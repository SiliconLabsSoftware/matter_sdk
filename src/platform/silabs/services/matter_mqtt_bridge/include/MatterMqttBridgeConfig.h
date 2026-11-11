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

#include "AttributeValueReader.h"
#include "ClusterAdapter.h"
#include "CommandInjector.h"
#include "DeviceIdSource.h"
#include "DeviceMqttProfile.h"
#include "MatterMqttCodec.h"
#include "MatterMqttValueConverter.h"
#include "MqttTransportPort.h"

#include <cstdint>

/**
 * Dependency injection bundle for MatterMqttBridge::Init. Null port pointers select built-in defaults
 * (MqttClientTransportPort, MatterMqttJsonCodec, CodeDrivenAttributeReader, MqttCommandInjector, FabricNodeIdSource).
 */
struct MatterMqttBridgeConfig
{
    MqttTransportPort * transport             = nullptr;
    MatterMqttCodec * codec                   = nullptr;
    ClusterAdapterRegistry * adapters         = nullptr;
    const DeviceMqttProfile * profile         = nullptr;
    AttributeValueReader * reader             = nullptr;
    CommandInjector * injector                = nullptr;
    DeviceIdSource * deviceId                 = nullptr;
    uint8_t commandQos                        = 0;
    uint8_t reportQos                         = 0;
    bool reportRetained                       = false;
};
