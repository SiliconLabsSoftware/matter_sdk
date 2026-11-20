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

#include "mqtt_client.h"

#include <cmsis_os2.h>
#include <cstdint>
#include <lib/core/CHIPError.h>

/**
 * @brief Matter app façade over Silabs MqttClient (static API, no instance).
 *
 * Lifecycle driven from BaseApplication Wi-Fi connectivity events:
 * Init on Wi-Fi up (Connect chained from Init completion), Disconnect on Wi-Fi lost.
 * Broker-down Connect failures retry with exponential backoff from sl_matter_mqtt_config.h
 * using a one-shot CMSIS-RTOS timer.
 *
 * Use GetMqttClient() for Subscribe / Publish / SetSubscriptionCallback and other
 * direct MqttClient operations after Connect succeeds.
 */
class MatterMqttClient
{
public:
    MatterMqttClient() = delete;

    static CHIP_ERROR Init();
    static CHIP_ERROR Connect();
    static CHIP_ERROR Disconnect();

    static chip::DeviceLayer::Silabs::MqttClient & GetMqttClient() { return sMqttClient; }

private:
    static CHIP_ERROR EnsureReconnectTimer();
    static void ResetReconnectState();
    static void ScheduleConnectRetry();

    static void OnInitDone(CHIP_ERROR result, void * context);
    static void OnConnectDone(CHIP_ERROR result, void * context);
    static void OnDisconnectDone(CHIP_ERROR result, void * context);
    static void RetryConnectHandler(void * argument);

    static chip::DeviceLayer::Silabs::MqttClient sMqttClient;
    static osTimerId_t sReconnectTimer;
    static uint8_t sReconnectCount;
};
