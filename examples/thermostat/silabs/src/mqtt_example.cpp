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

#include <lib/support/CodeUtils.h>
#include <lib/support/Span.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>

#include <cstring>

namespace {

using chip::ByteSpan;
using chip::DeviceLayer::ChipDeviceEvent;
using chip::DeviceLayer::kConnectivity_Established;
using chip::DeviceLayer::kConnectivity_Lost;
using chip::DeviceLayer::PlatformMgr;
using chip::DeviceLayer::Silabs::MqttBroker;
using chip::DeviceLayer::Silabs::MqttClient;
using chip::DeviceLayer::Silabs::MqttClientConfig;
namespace DeviceEventType = chip::DeviceLayer::DeviceEventType;

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

void OnPlatformEvent(const ChipDeviceEvent * event, intptr_t /* arg */)
{
    VerifyOrReturn(event != nullptr);

    switch (event->Type)
    {
    case DeviceEventType::kWiFiConnectivityChange:
        if (event->WiFiConnectivityChange.Result == kConnectivity_Established)
        {
            ChipLogProgress(DeviceLayer, "MQTT demo: WiFi Connected");
        }
        else if (event->WiFiConnectivityChange.Result == kConnectivity_Lost)
        {
            ChipLogProgress(DeviceLayer, "MQTT demo: WiFi Disconnected");
        }
        break;

    case DeviceEventType::kCommissioningComplete:
        ChipLogProgress(DeviceLayer, "MQTT demo: Commissioning Complete");
        break;

    case DeviceEventType::kSecureSessionEstablished:
        ChipLogProgress(DeviceLayer, "MQTT demo: Commissioning Started");
        break;

    default:
        break;
    }
}

void OnConnectDone(CHIP_ERROR result, void * /* context */)
{
    ChipLogDetail(DeviceLayer, "MQTT demo: OnConnectDone: %" CHIP_ERROR_FORMAT, result.Format());
}

void OnInitDone(CHIP_ERROR result, void * /* context */)
{
    ChipLogDetail(DeviceLayer, "MQTT demo: OnInitDone: %" CHIP_ERROR_FORMAT, result.Format());
    if (!gMqttsClient.IsConnected())
    {
        const MqttBroker broker = {
            .brokerIp    = kMqttBrokerIp,
            .tlsHostname = kMqttTlsHostname,
            .brokerPort  = kMqttBrokerPort,
            .clientPort  = kMqttClientPort,
        };
        CHIP_ERROR err = gMqttsClient.Connect(broker, OnConnectDone);
        LogErrorOnFailure(err);
    }
}

void OnDisconnectDone(CHIP_ERROR result, void * /* context */)
{
    ChipLogDetail(DeviceLayer, "MQTT demo: OnDisconnectDone: %" CHIP_ERROR_FORMAT, result.Format());
}

void OnSubscribeDone(CHIP_ERROR result, void * /* context */)
{
    ChipLogDetail(DeviceLayer, "MQTT demo: OnSubscribeDone: %" CHIP_ERROR_FORMAT, result.Format());
}

void OnPublishDone(CHIP_ERROR result, void * /* context */)
{
    ChipLogDetail(DeviceLayer, "MQTT demo: OnPublishDone: %" CHIP_ERROR_FORMAT, result.Format());
}

void OnMqttMessage(const char * topic, ByteSpan payload, void * /* context */)
{
    ChipLogProgress(DeviceLayer, "MQTT demo: message on %s: %.*s", topic != nullptr ? topic : "(null)",
                    static_cast<int>(payload.size()), reinterpret_cast<const char *>(payload.data()));
}

CHIP_ERROR SubscribePublish()
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    ChipLogProgress(DeviceLayer, "MQTT demo: subscribing to topic %s", kMqttTopic);
    err = gMqttsClient.Subscribe(kMqttTopic, OnSubscribeDone);
    ReturnErrorAndLogOnFailure(err, DeviceLayer, "MQTT demo: Subscribe failed: %" CHIP_ERROR_FORMAT, err.Format());

    ChipLogProgress(DeviceLayer, "MQTT demo: publishing message to topic %s", kMqttTopic);
    const ByteSpan payload(reinterpret_cast<const uint8_t *>(kMqttPublishMessage), strlen(kMqttPublishMessage));
    err = gMqttsClient.Publish(kMqttTopic, payload, false, OnPublishDone);
    ReturnErrorAndLogOnFailure(err, DeviceLayer, "MQTT demo: Publish failed: %" CHIP_ERROR_FORMAT, err.Format());

    return err;
}

} // namespace

sl_status_t mqtt_client_demo_start(void)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    if (!gMqttsClient.IsInitialized())
    {
        err = PlatformMgr().AddEventHandler(OnPlatformEvent, 0);
        VerifyOrReturnError(
            err == CHIP_NO_ERROR, SL_STATUS_FAIL,
            ChipLogError(DeviceLayer, "MQTT demo: OnPlatformEvent register failed: %" CHIP_ERROR_FORMAT, err.Format()));

        // Init is queued to the service thread, so Start must happen first when needed.
        if (!gMqttsClient.IsRunning())
        {
            ChipLogProgress(DeviceLayer, "MQTT demo: starting service");
            err = gMqttsClient.Start();
            VerifyOrReturnError(err == CHIP_NO_ERROR, SL_STATUS_FAIL,
                                ChipLogError(DeviceLayer, "MQTT demo: Start failed: %" CHIP_ERROR_FORMAT, err.Format()));

            gMqttsClient.SetSubscriptionCallback(OnMqttMessage, nullptr);
        }

        // Subscribe/Publish without an explicit QoS use MqttClientConfig::subQoS / pubQoS.
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

        ChipLogProgress(DeviceLayer, "MQTT demo: initializing client");
        err = gMqttsClient.Init(config, OnInitDone);
        VerifyOrReturnError(err == CHIP_NO_ERROR, SL_STATUS_FAIL,
                            ChipLogError(DeviceLayer, "MQTT demo: Init failed: %" CHIP_ERROR_FORMAT, err.Format()));
    }
    else
    {
        ChipLogProgress(DeviceLayer, "MQTT demo: already initialized");
        OnInitDone(CHIP_NO_ERROR, nullptr);
    }

    ChipLogProgress(DeviceLayer, "MQTT demo: ready (use demo mqtt for subscribe/publish)");
    // Prior attempt may have connected then failed on subscribe/publish; skip reconnect.
    return SL_STATUS_OK;
}

sl_status_t mqtt_client_demo_run(void)
{
    VerifyOrReturnError(gMqttsClient.IsRunning() && gMqttsClient.IsInitialized(), SL_STATUS_NOT_INITIALIZED,
                        ChipLogError(DeviceLayer, "MQTT demo: not ready; wait for connectivity start first"));

    const CHIP_ERROR err = SubscribePublish();
    LogErrorOnFailure(err);

    ChipLogProgress(DeviceLayer, "MQTT demo: completed (auto-yield keeps session alive)");
    return SL_STATUS_OK;
}

sl_status_t mqtt_client_demo_stop(void)
{
    // Keep Start/Init state so a later connectivity event only needs Connect+Subscribe+Publish.
    VerifyOrReturnError(gMqttsClient.IsRunning() && gMqttsClient.IsConnected(), SL_STATUS_OK);

    const CHIP_ERROR err = gMqttsClient.Disconnect(OnDisconnectDone);
    LogErrorOnFailure(err);

    ChipLogProgress(DeviceLayer, "MQTT demo: disconnected");
    return SL_STATUS_OK;
}

void MqttRunAppEvent(AppEvent * /* aEvent */)
{
    VerifyOrReturn(SL_STATUS_OK == mqtt_client_demo_run(), ChipLogError(DeviceLayer, "MQTT demo: run failed"));
}
