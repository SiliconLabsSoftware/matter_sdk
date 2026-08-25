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

#include "mqtt_example.h"

#include "mqtt_client.h"

#include "cacert.h"
#include "cmsis_os2.h"

#include <lib/support/Span.h>
#include <lib/support/logging/CHIPLogging.h>

#include <cstring>

namespace {

using chip::ByteSpan;
using chip::DeviceLayer::Silabs::MqttBroker;
using chip::DeviceLayer::Silabs::MqttClient;
using chip::DeviceLayer::Silabs::MqttClientConfig;
using chip::DeviceLayer::Silabs::MqttQoS;

// Please fill in the details for your own MQTT broker.
constexpr char kMqttBrokerIp[]       = MQTT_BROKER_IP;    // The IP address of your MQTT broker
constexpr char kMqttTlsHostname[]    = MQTT_TLS_HOSTNAME; // The TLS hostname of your MQTT broker
constexpr uint16_t kMqttBrokerPort   = MQTT_BROKER_PORT;
constexpr uint16_t kMqttClientPort   = MQTT_CLIENT_PORT;
constexpr char kMqttClientId[]       = MQTT_CLIENT_ID;
constexpr char kMqttUsername[]       = MQTT_USERNAME;
constexpr char kMqttPassword[]       = MQTT_PASSWORD;
constexpr char kMqttTopic[]          = MQTT_TOPIC;
constexpr char kMqttPublishMessage[] = MQTT_PUBLISH_MESSAGE;

MqttClient gMqttsClient;
volatile bool gOpDone = false;
CHIP_ERROR gOpResult  = CHIP_NO_ERROR;

void OnOperationDone(CHIP_ERROR result, void * /* context */)
{
    gOpResult = result;
    gOpDone   = true;
}

void OnMqttMessage(const char * topic, ByteSpan payload, void * /* context */)
{
    ChipLogProgress(DeviceLayer, "MQTT demo message on %s: %.*s", topic != nullptr ? topic : "(null)",
                    static_cast<int>(payload.size()), reinterpret_cast<const char *>(payload.data()));
}

// Must yield: demo may run above mqtt_client priority; osDelay lets the service thread run.
CHIP_ERROR RunOperation(CHIP_ERROR queueResult)
{
    if (queueResult != CHIP_NO_ERROR)
    {
        return queueResult;
    }

    while (!gOpDone)
    {
        osDelay(10);
    }

    return gOpResult;
}

} // namespace

sl_status_t mqtt_client_demo_start(void)
{
    if (gMqttsClient.IsRunning())
    {
        return SL_STATUS_ALREADY_INITIALIZED;
    }

    const MqttClientConfig config = {
        .useTls               = true,
        .clientId             = kMqttClientId,
        .username             = kMqttUsername,
        .password             = kMqttPassword,
        .keepAliveIntervalSec = 100,
        .commandTimeoutMs     = 20000,
        .mqttVersion          = 4,
        .cleanSession         = true,
        .willEnable           = false,
        .tlsCaCert            = reinterpret_cast<const uint8_t *>(kCaCertExample),
        .tlsCaCertLen         = sizeof(kCaCertExample),
    };

    const MqttBroker broker = {
        .brokerIp    = kMqttBrokerIp,
        .tlsHostname = kMqttTlsHostname,
        .brokerPort  = kMqttBrokerPort,
        .clientPort  = kMqttClientPort,
    };

    CHIP_ERROR err = gMqttsClient.Start();
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "MQTT Start failed: %" CHIP_ERROR_FORMAT, err.Format());
        return SL_STATUS_FAIL;
    }

    gMqttsClient.SetSubscriptionCallback(OnMqttMessage, nullptr);

    ChipLogProgress(DeviceLayer, "MQTT demo starting");

    gOpDone = false;
    err     = RunOperation(gMqttsClient.Init(config, OnOperationDone));
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "MQTT Init failed: %" CHIP_ERROR_FORMAT, err.Format());
        return SL_STATUS_FAIL;
    }

    gOpDone = false;
    err     = RunOperation(gMqttsClient.Connect(broker, OnOperationDone));
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "MQTT Connect failed: %" CHIP_ERROR_FORMAT, err.Format());
        return SL_STATUS_FAIL;
    }

    gOpDone = false;
    err     = RunOperation(gMqttsClient.Subscribe(kMqttTopic, MqttQoS::QoS1, OnOperationDone));
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "MQTT Subscribe failed: %" CHIP_ERROR_FORMAT, err.Format());
        return SL_STATUS_FAIL;
    }

    const ByteSpan payload(reinterpret_cast<const uint8_t *>(kMqttPublishMessage), strlen(kMqttPublishMessage));
    gOpDone = false;
    err     = RunOperation(gMqttsClient.Publish(kMqttTopic, payload, MqttQoS::QoS1, false, OnOperationDone));
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "MQTT Publish failed: %" CHIP_ERROR_FORMAT, err.Format());
        return SL_STATUS_FAIL;
    }

    ChipLogProgress(DeviceLayer, "MQTT demo completed (auto-yield keeps session alive)");
    return SL_STATUS_OK;
}
