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

void OnDisconnectDone(CHIP_ERROR result, void * /* context */)
{
    if (result != CHIP_NO_ERROR && result != CHIP_ERROR_INCORRECT_STATE && result != CHIP_ERROR_BUSY)
    {
        ChipLogError(DeviceLayer, "MQTT Disconnect failed: %" CHIP_ERROR_FORMAT, result.Format());
        return;
    }
    ChipLogProgress(DeviceLayer, "MQTT demo disconnected");
}

void OnMqttMessage(const char * topic, ByteSpan payload, void * /* context */)
{
    ChipLogProgress(DeviceLayer, "MQTT demo message on %s: %.*s", topic != nullptr ? topic : "(null)",
                    static_cast<int>(payload.size()), reinterpret_cast<const char *>(payload.data()));
}

// Must not run on the CHIP event-loop thread: osDelay would starve the platform queue.
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

CHIP_ERROR ConnectSubscribePublish(const MqttBroker & broker)
{
    gOpDone        = false;
    CHIP_ERROR err = RunOperation(gMqttsClient.Connect(broker, OnOperationDone));
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "MQTT Connect failed: %" CHIP_ERROR_FORMAT, err.Format());
        return err;
    }

    gOpDone = false;
    err     = RunOperation(gMqttsClient.Subscribe(kMqttTopic, OnOperationDone));
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "MQTT Subscribe failed: %" CHIP_ERROR_FORMAT, err.Format());
        return err;
    }

    const ByteSpan payload(reinterpret_cast<const uint8_t *>(kMqttPublishMessage), strlen(kMqttPublishMessage));
    gOpDone = false;
    err     = RunOperation(gMqttsClient.Publish(kMqttTopic, payload, false, OnOperationDone));
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "MQTT Publish failed: %" CHIP_ERROR_FORMAT, err.Format());
        return err;
    }

    return CHIP_NO_ERROR;
}

} // namespace

// Wi-Fi callback / MatterWifiTask: must stay non-blocking (no ScheduleWork + wait).
extern "C" void MatterWifiOnStationLinkDown(void)
{
    const sl_status_t status = mqtt_client_demo_stop();
    if (status != SL_STATUS_OK)
    {
        ChipLogError(DeviceLayer, "mqtt_client_demo_stop on link down failed: 0x%lx", static_cast<unsigned long>(status));
    }
}

sl_status_t mqtt_client_demo_start(void)
{
    const MqttBroker broker = {
        .brokerIp    = kMqttBrokerIp,
        .tlsHostname = kMqttTlsHostname,
        .brokerPort  = kMqttBrokerPort,
        .clientPort  = kMqttClientPort,
    };

    if (gMqttsClient.IsConnected())
    {
        ChipLogProgress(DeviceLayer, "MQTT demo already connected");
        return SL_STATUS_OK;
    }

    CHIP_ERROR err = CHIP_NO_ERROR;

    if (!gMqttsClient.IsRunning())
    {
        err = gMqttsClient.Start();
        if (err != CHIP_NO_ERROR)
        {
            ChipLogError(DeviceLayer, "MQTT Start failed: %" CHIP_ERROR_FORMAT, err.Format());
            return SL_STATUS_FAIL;
        }
        gMqttsClient.SetSubscriptionCallback(OnMqttMessage, nullptr);
    }

    if (!gMqttsClient.IsInitialized())
    {
        // QoS defaults come from MqttClientConfig in mqtt_client.h (.qos / .willQoS).
        const MqttClientConfig config = {
            .useTls               = true,
            .clientId             = kMqttClientId,
            .username             = kMqttUsername,
            .password             = kMqttPassword,
            .keepAliveIntervalSec = 100,
            .commandTimeoutMs     = 20000,
            .mqttVersion          = 4,
            .cleanSession         = mConfig.cleanSession ? 1 : 0;,
            .willEnable           = false,
            .tlsCaCert            = reinterpret_cast<const uint8_t *>(kCaCertExample),
            .tlsCaCertLen         = sizeof(kCaCertExample),
        };

        ChipLogProgress(DeviceLayer, "MQTT demo starting");

        gOpDone = false;
        err     = RunOperation(gMqttsClient.Init(config, OnOperationDone));
        if (err != CHIP_NO_ERROR)
        {
            ChipLogError(DeviceLayer, "MQTT Init failed: %" CHIP_ERROR_FORMAT, err.Format());
            return SL_STATUS_FAIL;
        }
    }
    else
    {
        ChipLogProgress(DeviceLayer, "MQTT demo reconnecting");
    }

    err = ConnectSubscribePublish(broker);
    if (err != CHIP_NO_ERROR)
    {
        return SL_STATUS_FAIL;
    }

    ChipLogProgress(DeviceLayer, "MQTT demo completed (auto-yield keeps session alive)");
    return SL_STATUS_OK;
}

sl_status_t mqtt_client_demo_stop(void)
{
    if (!gMqttsClient.IsRunning() || !gMqttsClient.IsInitialized())
    {
        return SL_STATUS_OK;
    }

    if (!gMqttsClient.IsConnected())
    {
        // IdleYield may already have torn the session down after link loss.
        ChipLogProgress(DeviceLayer, "MQTT demo already disconnected");
        return SL_STATUS_OK;
    }

    // Non-blocking: safe from Wi-Fi / CHIP paths. Completion logged in OnDisconnectDone.
    ChipLogProgress(DeviceLayer, "MQTT demo disconnecting");
    CHIP_ERROR err = gMqttsClient.Disconnect(OnDisconnectDone);
    if (err != CHIP_NO_ERROR && err != CHIP_ERROR_INCORRECT_STATE && err != CHIP_ERROR_BUSY)
    {
        ChipLogError(DeviceLayer, "MQTT Disconnect queue failed: %" CHIP_ERROR_FORMAT, err.Format());
        return SL_STATUS_FAIL;
    }

    return SL_STATUS_OK;
}
