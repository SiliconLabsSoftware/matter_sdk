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

#include "cacert.pem.h"
#include "cmsis_os2.h"

#include <lib/support/CodeUtils.h>
#include <lib/support/Span.h>
#include <lib/support/logging/CHIPLogging.h>

#include <cstring>

namespace {

using chip::ByteSpan;
using chip::DeviceLayer::Silabs::MqttBroker;
using chip::DeviceLayer::Silabs::MqttClient;
using chip::DeviceLayer::Silabs::MqttClientConfig;
using chip::DeviceLayer::Silabs::MqttQos;

constexpr char kMqttBrokerIp[]       = "192.168.0.191";
constexpr char kMqttTlsHostname[]    = "example.com";
constexpr uint16_t kMqttBrokerPort   = 8883;
constexpr uint16_t kMqttClientPort   = 5003;
constexpr char kMqttClientId[]       = "WISECONNECT_SDK_TOPIC";
constexpr char kMqttUsername[]       = "username";
constexpr char kMqttPassword[]       = "password";
constexpr char kMqttTopic[]          = "THERMOSTAT-DATA";
constexpr char kMqttPublishMessage[] = "THIS IS MQTT CLIENT DEMO FROM APPLICATION";
constexpr uint32_t kMqttYieldTimeoutMs = 60000;

MqttClient gMqttClient;

osSemaphoreId_t gDoneLock = nullptr;
volatile bool gMessageReceived = false;

struct OperationWaitContext
{
    osSemaphoreId_t lock;
    CHIP_ERROR result;
};

void OnOperationDone(CHIP_ERROR result, void * context)
{
    auto * waitCtx  = static_cast<OperationWaitContext *>(context);
    waitCtx->result = result;
    osSemaphoreRelease(waitCtx->lock);
}

void OnMqttMessage(const char * topic, ByteSpan payload, void * /* context */)
{
    ChipLogProgress(DeviceLayer, "MQTT message on %s: %.*s", topic != nullptr ? topic : "(null)",
                    static_cast<int>(payload.size()), reinterpret_cast<const char *>(payload.data()));
    gMessageReceived = true;
}

CHIP_ERROR WaitForCallback(OperationWaitContext & waitCtx)
{
    const osStatus_t status = osSemaphoreAcquire(waitCtx.lock, osWaitForever);
    VerifyOrReturnError(status == osOK, CHIP_ERROR_INTERNAL);
    return waitCtx.result;
}

CHIP_ERROR RunQueuedOperation(CHIP_ERROR queueResult, OperationWaitContext & waitCtx)
{
    if (queueResult != CHIP_NO_ERROR)
    {
        return queueResult;
    }
    return WaitForCallback(waitCtx);
}

CHIP_ERROR RunMqttExample()
{
    ChipLogProgress(DeviceLayer, "MQTT demo starting");

    OperationWaitContext waitCtx = { .lock = gDoneLock, .result = CHIP_NO_ERROR };
    gMessageReceived             = false;

    const MqttBroker broker = {
        .brokerIp    = kMqttBrokerIp,
        .tlsHostname = kMqttTlsHostname,
        .brokerPort  = kMqttBrokerPort,
        .clientPort  = kMqttClientPort,
    };

    CHIP_ERROR err = RunQueuedOperation(gMqttClient.Connect(broker, OnOperationDone, &waitCtx), waitCtx);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "MQTT Connect failed: %" CHIP_ERROR_FORMAT, err.Format());
        return err;
    }

    err = RunQueuedOperation(gMqttClient.Subscribe(kMqttTopic, MqttQos::AtLeastOnce, OnOperationDone, &waitCtx), waitCtx);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "MQTT Subscribe failed: %" CHIP_ERROR_FORMAT, err.Format());
        return err;
    }

    const ByteSpan payload(reinterpret_cast<const uint8_t *>(kMqttPublishMessage), strlen(kMqttPublishMessage));
    err = RunQueuedOperation(
        gMqttClient.Publish(kMqttTopic, payload, MqttQos::AtLeastOnce, false, OnOperationDone, &waitCtx), waitCtx);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "MQTT Publish failed: %" CHIP_ERROR_FORMAT, err.Format());
        return err;
    }

    ChipLogProgress(DeviceLayer, "MQTT waiting for message on topic %s", kMqttTopic);
    while (!gMessageReceived)
    {
        err = RunQueuedOperation(gMqttClient.Yield(kMqttYieldTimeoutMs, OnOperationDone, &waitCtx), waitCtx);
        if (err != CHIP_NO_ERROR)
        {
            ChipLogError(DeviceLayer, "MQTT Yield failed: %" CHIP_ERROR_FORMAT, err.Format());
            return err;
        }
    }

    err = RunQueuedOperation(gMqttClient.Unsubscribe(kMqttTopic, OnOperationDone, &waitCtx), waitCtx);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "MQTT Unsubscribe failed: %" CHIP_ERROR_FORMAT, err.Format());
        return err;
    }

    err = RunQueuedOperation(gMqttClient.Disconnect(OnOperationDone, &waitCtx), waitCtx);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "MQTT Disconnect failed: %" CHIP_ERROR_FORMAT, err.Format());
        return err;
    }

    ChipLogProgress(DeviceLayer, "MQTT demo completed");
    return CHIP_NO_ERROR;
}

} // namespace

extern "C" sl_status_t mqtt_client_demo_start(void)
{
    if (gMqttClient.IsRunning())
    {
        return SL_STATUS_ALREADY_INITIALIZED;
    }

    if (gDoneLock == nullptr)
    {
        gDoneLock = osSemaphoreNew(1, 0, nullptr);
        if (gDoneLock == nullptr)
        {
            ChipLogError(DeviceLayer, "MQTT demo lock create failed");
            return SL_STATUS_FAIL;
        }
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
        .tlsCaCert            = reinterpret_cast<const uint8_t *>(cacert),
        .tlsCaCertLen         = sizeof(cacert),
    };

    CHIP_ERROR err = gMqttClient.Start();
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "MQTT Start failed: %" CHIP_ERROR_FORMAT, err.Format());
        return SL_STATUS_FAIL;
    }

    gMqttClient.SetMessageCallback(OnMqttMessage, nullptr);

    OperationWaitContext waitCtx = { .lock = gDoneLock, .result = CHIP_NO_ERROR };
    err                          = RunQueuedOperation(gMqttClient.Init(config, OnOperationDone, &waitCtx), waitCtx);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "MQTT Init failed: %" CHIP_ERROR_FORMAT, err.Format());
        CHIP_ERROR stopErr = gMqttClient.Stop();
        if (stopErr != CHIP_NO_ERROR)
        {
            ChipLogError(DeviceLayer, "MQTT Stop failed: %" CHIP_ERROR_FORMAT, stopErr.Format());
        }
        return SL_STATUS_FAIL;
    }

    err = RunMqttExample();

    CHIP_ERROR deinitErr = RunQueuedOperation(gMqttClient.Deinit(OnOperationDone, &waitCtx), waitCtx);
    if (deinitErr != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "MQTT Deinit failed: %" CHIP_ERROR_FORMAT, deinitErr.Format());
    }

    CHIP_ERROR stopErr = gMqttClient.Stop();
    if (stopErr != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "MQTT Stop failed: %" CHIP_ERROR_FORMAT, stopErr.Format());
    }

    return (err == CHIP_NO_ERROR && deinitErr == CHIP_NO_ERROR && stopErr == CHIP_NO_ERROR) ? SL_STATUS_OK : SL_STATUS_FAIL;
}
