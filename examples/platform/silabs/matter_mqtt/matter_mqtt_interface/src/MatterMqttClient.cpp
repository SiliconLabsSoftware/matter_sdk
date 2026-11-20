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

#include "MatterMqttClient.h"

#include "sl_matter_mqtt_config.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <inttypes.h>

#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/ConnectivityManager.h>
#include <sl_cmsis_os2_common.h>

#if defined(SL_MATTER_ENABLE_MQTT_BRIDGE) && SL_MATTER_ENABLE_MQTT_BRIDGE
#include <MatterMqttBridge.h>
#include <MatterMqttBridgeConfigImpl.h>
#endif // SL_MATTER_ENABLE_MQTT_BRIDGE

namespace {

using chip::ByteSpan;
using chip::CharSpan;
using chip::DeviceLayer::ConnectivityMgr;
using chip::DeviceLayer::Silabs::MqttBroker;
using chip::DeviceLayer::Silabs::MqttClientConfig;

static_assert(UINT32_MAX / SL_MATTER_MQTT_RECONNECT_INTERVAL_MS >= (1u << SL_MATTER_MQTT_RECONNECT_MAX_BACKOFF),
              "MQTT reconnect backoff multiply will overflow uint32_t");

constexpr char kMqttBrokerEndpoint[] = SL_MATTER_MQTT_BROKER_ENDPOINT;
constexpr char kMqttTlsHostname[]    = SL_MATTER_MQTT_TLS_HOSTNAME;
constexpr uint16_t kMqttBrokerPort   = SL_MATTER_MQTT_BROKER_PORT;
constexpr uint16_t kMqttClientPort   = SL_MATTER_MQTT_CLIENT_PORT;
constexpr char kMqttClientId[]       = SL_MATTER_MQTT_CLIENT_ID;
constexpr char kMqttUsername[]       = SL_MATTER_MQTT_CLIENT_USERNAME;
constexpr char kMqttPassword[]       = SL_MATTER_MQTT_CLIENT_PASSWORD;

constexpr char kMqttTlsCaCertificate[]     = SL_MATTER_MQTT_TLS_CA_CERTIFICATE;
constexpr char kMqttTlsClientCertificate[] = SL_MATTER_MQTT_TLS_CLIENT_CERTIFICATE;
constexpr char kMqttTlsClientKey[]         = SL_MATTER_MQTT_TLS_CLIENT_KEY;

#if defined(SL_MATTER_ENABLE_MQTT_BRIDGE) && SL_MATTER_ENABLE_MQTT_BRIDGE
void MqttInboundDispatcher(CharSpan topic, ByteSpan payload, void * context);
#endif // SL_MATTER_ENABLE_MQTT_BRIDGE

} // namespace

chip::DeviceLayer::Silabs::MqttClient MatterMqttClient::sMqttClient;
osTimerId_t MatterMqttClient::sReconnectTimer = nullptr;
uint8_t MatterMqttClient::sReconnectCount     = 0;

CHIP_ERROR MatterMqttClient::Init()
{
    if (sMqttClient.IsInitialized())
    {
        ChipLogProgress(DeviceLayer, "MatterMqttClient: already initialized");
        return Connect();
    }

    if (!sMqttClient.IsRunning())
    {
        ChipLogProgress(DeviceLayer, "MatterMqttClient: starting service");
        ReturnErrorOnFailure(sMqttClient.Start());
    }

    const MqttClientConfig config = {
        .useTls               = SL_MATTER_MQTT_TLS_ENABLED,
        .clientId             = kMqttClientId,
        .username             = kMqttUsername,
        .password             = kMqttPassword,
        .keepAliveIntervalSec = 100,
        .commandTimeoutMs     = 20000,
        .mqttVersion          = 4,
        .cleanSession         = true,
        .willEnable           = false,
        // Empty string means not provided; MqttClient skips zero-length credentials.
        .tlsCaCert     = reinterpret_cast<const uint8_t *>(kMqttTlsCaCertificate),
        .tlsCaCertLen  = strnlen(kMqttTlsCaCertificate, sizeof(kMqttTlsCaCertificate)),
        .clientCert    = reinterpret_cast<const uint8_t *>(kMqttTlsClientCertificate),
        .clientCertLen = strnlen(kMqttTlsClientCertificate, sizeof(kMqttTlsClientCertificate)),
        .clientKey     = reinterpret_cast<const uint8_t *>(kMqttTlsClientKey),
        .clientKeyLen  = strnlen(kMqttTlsClientKey, sizeof(kMqttTlsClientKey)),
    };

    ChipLogProgress(DeviceLayer, "MatterMqttClient: initializing client");
    return sMqttClient.Init(config, OnInitDone);
}

CHIP_ERROR MatterMqttClient::Connect()
{
    const MqttBroker broker = {
        .brokerEndpoint = kMqttBrokerEndpoint,
        .tlsHostname    = kMqttTlsHostname,
        .brokerPort     = kMqttBrokerPort,
        .clientPort     = kMqttClientPort,
    };

    ChipLogProgress(DeviceLayer, "MatterMqttClient: connecting to %s:%u", kMqttBrokerEndpoint,
                    static_cast<unsigned>(kMqttBrokerPort));
    return sMqttClient.Connect(broker, OnConnectDone);
}

CHIP_ERROR MatterMqttClient::Disconnect()
{
    ResetReconnectState();

    ChipLogProgress(DeviceLayer, "MatterMqttClient: disconnecting");
    return sMqttClient.Disconnect(OnDisconnectDone);
}

CHIP_ERROR MatterMqttClient::EnsureReconnectTimer()
{
    VerifyOrReturnError(sReconnectTimer == nullptr, CHIP_NO_ERROR);

    sReconnectTimer = osTimerNew(RetryConnectHandler, osTimerOnce, nullptr, nullptr);
    VerifyOrReturnError(sReconnectTimer != nullptr, CHIP_ERROR_NO_MEMORY,
                        ChipLogError(DeviceLayer, "MatterMqttClient: reconnect timer create failed"));
    return CHIP_NO_ERROR;
}

void MatterMqttClient::ResetReconnectState()
{
    sReconnectCount = 0;
    if (sReconnectTimer != nullptr)
    {
        (void) osTimerStop(sReconnectTimer);
    }
}

void MatterMqttClient::ScheduleConnectRetry()
{
    const uint32_t delayMs = SL_MATTER_MQTT_RECONNECT_INTERVAL_MS * (1u << sReconnectCount);

    ChipLogProgress(DeviceLayer, "Next MQTT broker reconnect in %" PRIu32 " ms", delayMs);

    if (EnsureReconnectTimer() != CHIP_NO_ERROR)
    {
        return;
    }

    if (osTimerStart(sReconnectTimer, pdMS_TO_TICKS(delayMs)) != osOK)
    {
        ChipLogError(DeviceLayer, "MatterMqttClient: reconnect timer start failed");
        return;
    }

    sReconnectCount = static_cast<uint8_t>(
        std::min(static_cast<unsigned>(sReconnectCount + 1), static_cast<unsigned>(SL_MATTER_MQTT_RECONNECT_MAX_BACKOFF)));
}

void MatterMqttClient::OnInitDone(CHIP_ERROR result, void * /* context */)
{
    ChipLogDetail(DeviceLayer, "MatterMqttClient: OnInitDone: %" CHIP_ERROR_FORMAT, result.Format());
    VerifyOrReturn(result == CHIP_NO_ERROR);
    LogErrorOnFailure(Connect());
}

void MatterMqttClient::OnConnectDone(CHIP_ERROR result, void * /* context */)
{
    ChipLogDetail(DeviceLayer, "MatterMqttClient: OnConnectDone: %" CHIP_ERROR_FORMAT, result.Format());

    if (result == CHIP_NO_ERROR)
    {
        ResetReconnectState();
#if defined(SL_MATTER_ENABLE_MQTT_BRIDGE) && SL_MATTER_ENABLE_MQTT_BRIDGE
        sMqttClient.SetSubscriptionCallback(MqttInboundDispatcher, nullptr);
        LogErrorOnFailure(MatterMqttBridge::Init(sMqttClient, gPlatformMqttBridgeConfig));
#endif // SL_MATTER_ENABLE_MQTT_BRIDGE
        return;
    }

    VerifyOrReturn(ConnectivityMgr().IsWiFiStationConnected(),
                   ChipLogProgress(DeviceLayer, "MatterMqttClient: skip reconnect; Wi-Fi down"));
    ScheduleConnectRetry();
}

void MatterMqttClient::OnDisconnectDone(CHIP_ERROR result, void * /* context */)
{
    ChipLogDetail(DeviceLayer, "MatterMqttClient: OnDisconnectDone: %" CHIP_ERROR_FORMAT, result.Format());
#if defined(SL_MATTER_ENABLE_MQTT_BRIDGE) && SL_MATTER_ENABLE_MQTT_BRIDGE
    MatterMqttBridge::Shutdown();
#endif // SL_MATTER_ENABLE_MQTT_BRIDGE
}

void MatterMqttClient::RetryConnectHandler(void * /* argument */)
{
    VerifyOrReturn(ConnectivityMgr().IsWiFiStationConnected(),
                   ChipLogProgress(DeviceLayer, "MatterMqttClient: retry skipped; Wi-Fi down"));
    VerifyOrReturn(!sMqttClient.IsConnected(), ChipLogProgress(DeviceLayer, "MatterMqttClient: already connected"));
    LogErrorOnFailure(Connect());
}

#if defined(SL_MATTER_ENABLE_MQTT_BRIDGE) && SL_MATTER_ENABLE_MQTT_BRIDGE
namespace {

// Bridge first; log only when DispatchInbound returns false (non-command topic or pre-subscribe).
// Command topics are consumed (accepted or rejected with a logged reason).
void MqttInboundDispatcher(CharSpan topic, ByteSpan payload, void * context)
{
    bool status = MatterMqttBridge::DispatchInbound(topic, payload);
    VerifyOrReturn(status, ChipLogDetail(DeviceLayer, "MqttInboundDispatcher: topic %s not handled by bridge", topic.data()));
}

} // namespace
#endif // SL_MATTER_ENABLE_MQTT_BRIDGE
