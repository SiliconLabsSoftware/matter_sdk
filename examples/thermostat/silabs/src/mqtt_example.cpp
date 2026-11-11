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

#if SL_MATTER_ENABLE_MQTT_BRIDGE
#include <MatterMqttBridge.h>
#include <ThermostatMqttBridgeConfig.h>
#endif

#include "cacert.h"
#include "cmsis_os2.h"

#include <lib/support/CodeUtils.h>
#include <lib/support/Span.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>

#include <cstring>

namespace {

using chip::ByteSpan;
using chip::CharSpan;
using chip::DeviceLayer::ChipDeviceEvent;
using chip::DeviceLayer::kConnectivity_Established;
using chip::DeviceLayer::kConnectivity_Lost;
using chip::DeviceLayer::PlatformMgr;
using chip::DeviceLayer::Silabs::MqttBroker;
using chip::DeviceLayer::Silabs::MqttClient;
using chip::DeviceLayer::Silabs::MqttClientConfig;
namespace DeviceEventType = chip::DeviceLayer::DeviceEventType;

// Please fill in the details for your own MQTT broker.
constexpr char kMqttBrokerEndpoint[] = MQTT_BROKER_ENDPOINT; // DNS name or IPv4 of your MQTT broker
constexpr char kMqttTlsHostname[]      = MQTT_TLS_HOSTNAME;    // The TLS hostname of your MQTT broker
constexpr uint16_t kMqttBrokerPort     = MQTT_BROKER_PORT;
constexpr uint16_t kMqttClientPort     = MQTT_CLIENT_PORT;
constexpr char kMqttClientId[]         = MQTT_CLIENT_ID;
constexpr char kMqttUsername[]         = MQTT_USERNAME;
constexpr char kMqttPassword[]         = MQTT_PASSWORD;
constexpr char kMqttTopic[]            = MQTT_TOPIC;
constexpr char kMqttPublishMessage[]   = MQTT_PUBLISH_MESSAGE;

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

    case DeviceEventType::kSLSystemEventCommissioningStarted:
        ChipLogProgress(DeviceLayer, "MQTT demo: Commissioning Started");
        break;

    case DeviceEventType::kSLSystemEventCommissioningFailed:
        ChipLogProgress(DeviceLayer, "MQTT demo: Commissioning Failed");
        break;

    default:
        break;
    }
}

#if SL_MATTER_ENABLE_MQTT_BRIDGE
void MqttInboundDispatcher(CharSpan topic, ByteSpan payload, void * context);
#endif

void OnConnectDone(CHIP_ERROR result, void * /* context */)
{
    ChipLogDetail(DeviceLayer, "MQTT demo: OnConnectDone: %" CHIP_ERROR_FORMAT, result.Format());
#if SL_MATTER_ENABLE_MQTT_BRIDGE
    if (result == CHIP_NO_ERROR)
    {
        gMqttsClient.SetSubscriptionCallback(MqttInboundDispatcher, nullptr);
        CHIP_ERROR err = MatterMqttBridge::Init(gMqttsClient, kThermostatBridgeConfig);
        LogErrorOnFailure(err);
    }
#endif
}

void OnInitDone(CHIP_ERROR result, void * /* context */)
{
    ChipLogDetail(DeviceLayer, "MQTT demo: OnInitDone: %" CHIP_ERROR_FORMAT, result.Format());
    if (result == CHIP_NO_ERROR && !gMqttsClient.IsConnected())
    {
        const MqttBroker broker = {
            .brokerEndpoint = kMqttBrokerEndpoint,
            .tlsHostname    = kMqttTlsHostname,
            .brokerPort     = kMqttBrokerPort,
            .clientPort     = kMqttClientPort,
        };
        CHIP_ERROR err = gMqttsClient.Connect(broker, OnConnectDone);
        LogErrorOnFailure(err);
    }
}

void OnDisconnectDone(CHIP_ERROR result, void * /* context */)
{
    ChipLogDetail(DeviceLayer, "MQTT demo: OnDisconnectDone: %" CHIP_ERROR_FORMAT, result.Format());
#if SL_MATTER_ENABLE_MQTT_BRIDGE
    MatterMqttBridge::Shutdown();
    (void) result;
#endif
}

void OnSubscribeDone(CHIP_ERROR result, void * /* context */)
{
    ChipLogDetail(DeviceLayer, "MQTT demo: OnSubscribeDone: %" CHIP_ERROR_FORMAT, result.Format());
}

void OnPublishDone(CHIP_ERROR result, void * /* context */)
{
    ChipLogDetail(DeviceLayer, "MQTT demo: OnPublishDone: %" CHIP_ERROR_FORMAT, result.Format());
}

void OnMqttMessage(CharSpan topic, ByteSpan payload, void * /* context */)
{
    ChipLogProgress(DeviceLayer, "MQTT demo: message on %.*s: %.*s", static_cast<int>(topic.size()),
                    topic.data() != nullptr ? topic.data() : "(null)", static_cast<int>(payload.size()),
                    reinterpret_cast<const char *>(payload.data()));
}

#if SL_MATTER_ENABLE_MQTT_BRIDGE
// Bridge first; demo logger only for non-command topics (e.g. kMqttTopic) or pre-subscribe noise.
void MqttInboundDispatcher(CharSpan topic, ByteSpan payload, void * context)
{
    if (MatterMqttBridge::DispatchInbound(topic, payload))
    {
        return;
    }
    OnMqttMessage(topic, payload, context);
}
#endif

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

        if (!gMqttsClient.IsRunning())
        {
            ChipLogProgress(DeviceLayer, "MQTT demo: starting service");
            err = gMqttsClient.Start();
            VerifyOrReturnError(err == CHIP_NO_ERROR, SL_STATUS_FAIL,
                                ChipLogError(DeviceLayer, "MQTT demo: Start failed: %" CHIP_ERROR_FORMAT, err.Format()));

#if !SL_MATTER_ENABLE_MQTT_BRIDGE
            gMqttsClient.SetSubscriptionCallback(OnMqttMessage, nullptr);
#endif
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
