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

#include "mqtt_client.h"

#include "cmsis_os2.h"

#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>

#include <cinttypes>
#include <cstdlib>
#include <cstring>

namespace chip {
namespace DeviceLayer {
namespace Silabs {

MqttClient * MqttClient::sActiveClient = nullptr;

CHIP_ERROR MqttClient::MapPahoStatus(int status)
{
    return (status == SUCCESS) ? CHIP_NO_ERROR : CHIP_ERROR_INTERNAL;
}

enum QoS MqttClient::ToPahoQos(MqttQoS qos)
{
    switch (qos)
    {
    case MqttQoS::QoS0:
        return QOS0;
    case MqttQoS::QoS2:
        return QOS2;
    case MqttQoS::QoS1:
    default:
        return QOS1;
    }
}

bool MqttClient::IsRunning() const
{
    return mThreadId != nullptr;
}

bool MqttClient::HasQueuedMessages() const
{
    VerifyOrReturnValue(mMessageQueue != nullptr, false);
    return osMessageQueueGetCount(static_cast<osMessageQueueId_t>(mMessageQueue)) > 0;
}

bool MqttClient::IsBusy() const
{
    return HasQueuedMessages();
}

void MqttClient::SetSubscriptionCallback(MqttSubscriptionCallback callback, void * context)
{
    mMessageCallback        = callback;
    mMessageCallbackContext = context;
}

void MqttClient::InvokeCallback(const ServiceMessage & message, CHIP_ERROR error)
{
    if (message.callback != nullptr)
    {
        message.callback(error, message.context);
    }
}

void MqttClient::PahoMessageHandler(MessageData * md)
{
    MqttClient * self = sActiveClient;
    VerifyOrReturn(self != nullptr);
    VerifyOrReturn(md != nullptr && md->message != nullptr);

    char * topic = nullptr;
    if (md->topicName != nullptr)
    {
        if (md->topicName->cstring != nullptr)
        {
            topic = md->topicName->cstring;
        }
        else if (md->topicName->lenstring.data != nullptr && md->topicName->lenstring.len > 0)
        {
            topic = static_cast<char *>(malloc(md->topicName->lenstring.len + 1));
            if (topic != nullptr)
            {
                memcpy(topic, md->topicName->lenstring.data, md->topicName->lenstring.len);
                topic[md->topicName->lenstring.len] = '\0';
            }
        }
    }
    ChipLogDetail(DeviceLayer, "[MQTT] message received on topic: %s", topic != nullptr ? topic : "unknown");

    const ByteSpan payload(static_cast<const uint8_t *>(md->message->payload), md->message->payloadlen);
    if (self->mMessageCallback != nullptr)
    {
        self->mMessageCallback(topic, payload, self->mMessageCallbackContext);
    }
    if (topic != nullptr && (md->topicName == nullptr || md->topicName->cstring == nullptr))
    {
        free(topic);
    }
}

const char * GetNetworkErrorString(int status)
{
    switch (status)
    {
    case NETWORK_ERROR_NULL_STRUCTURE:
        return "MQTT network connect failed: null structure";
    case NETWORK_ERROR_NULL_ADDRESS:
        return "MQTT broker address is NULL";
    case NETWORK_ERROR_INVALID_TYPE:
        return "MQTT invalid transport type";
#if defined(NETWORK_ERROR_TLS_HOSTNAME_REQUIRED)
    case NETWORK_ERROR_TLS_HOSTNAME_REQUIRED:
        return "MQTT TLS requires NetworkSetTlsHostname before connect";
#endif // NETWORK_ERROR_TLS_HOSTNAME_REQUIRED
#if defined(NETWORK_ERROR_CONNECT_FAILED)
    case NETWORK_ERROR_CONNECT_FAILED:
        return "MQTT socket connect failed";
#endif // NETWORK_ERROR_CONNECT_FAILED
    default:
        return "MQTT TCP/TLS connect failed";
    }
}

void MqttClient::LogNetworkConnectError(int status)
{
    ChipLogError(DeviceLayer, "%s", GetNetworkErrorString(status));
}

// Frees only the pre-connect TLS stub (cert holder) allocated in ProcessConnect.
// After NetworkConnect succeeds, the transport owns the live context and frees it via
// NetworkDisconnect; do not raw-free an initialized TLS context here.
void MqttClient::FreeTlsContext()
{
#if MQTT_USE_HOST_LWIP_TLS && MQTT_TLS_ENABLE
    if (mNetwork.tls != nullptr)
    {
        free(mNetwork.tls);
        mNetwork.tls = nullptr;
    }
#endif // MQTT_USE_HOST_LWIP_TLS && MQTT_TLS_ENABLE
}

CHIP_ERROR MqttClient::PostMessage(const ServiceMessage & message)
{
    VerifyOrReturnError(mMessageQueue != nullptr, CHIP_ERROR_INCORRECT_STATE);

    const osStatus_t status = osMessageQueuePut(static_cast<osMessageQueueId_t>(mMessageQueue), &message, 0, 0);
    if (status != osOK)
    {
        ChipLogError(DeviceLayer, "[MQTT] unable to post message: 0x%" PRIx32, static_cast<uint32_t>(status));
        return CHIP_ERROR_INTERNAL;
    }
    return CHIP_NO_ERROR;
}

void MqttClient::DrainQueuedOperations(CHIP_ERROR reason)
{
    VerifyOrReturn(mMessageQueue != nullptr);

    auto messageQueue = static_cast<osMessageQueueId_t>(mMessageQueue);
    ServiceMessage message{};
    bool stopPending = false;

    while (osMessageQueueGet(messageQueue, &message, nullptr, 0) == osOK)
    {
        if (message.operation == Operation::Stop)
        {
            stopPending = true;
            continue;
        }
        InvokeCallback(message, reason);
    }

    if (stopPending)
    {
        ServiceMessage stopMessage{};
        stopMessage.operation = Operation::Stop;
        LogErrorOnFailure(PostMessage(stopMessage));
    }
}

void MqttClient::ProcessMessage(const ServiceMessage & message)
{
    CHIP_ERROR err = CHIP_ERROR_INTERNAL;

    switch (message.operation)
    {
    case Operation::Init:
        err = ProcessInit();
        break;
    case Operation::Deinit:
        DrainQueuedOperations(CHIP_ERROR_CANCELLED);
        err = ProcessDeinit();
        break;
    case Operation::Connect:
        err = ProcessConnect();
        break;
    case Operation::Disconnect:
        DrainQueuedOperations(CHIP_ERROR_CANCELLED);
        err = ProcessDisconnect();
        break;
    case Operation::Subscribe:
        err = ProcessSubscribe(message);
        break;
    case Operation::Unsubscribe:
        err = ProcessUnsubscribe(message);
        break;
    case Operation::Publish:
        err = ProcessPublish(message);
        break;
    case Operation::Yield:
        err = ProcessYield(message);
        break;
    case Operation::Stop:
    default:
        err = CHIP_ERROR_INCORRECT_STATE;
        break;
    }

    InvokeCallback(message, err);
}

void MqttClient::ServiceThread(void * arg)
{
    auto * self = static_cast<MqttClient *>(arg);
    VerifyOrReturn(self != nullptr);

    auto messageQueue = static_cast<osMessageQueueId_t>(self->mMessageQueue);
    while (true)
    {
        ServiceMessage message{};
        // While connected, poll the queue (timeout 0) then MQTTYield so the socket is read
        // for subscribed publishes / keepalive. Do not block on the queue first — that starves
        // MQTTYield and drops inbound topic traffic until the wait expires.
        // While disconnected, block forever waiting for Init / Connect / Stop.
        const uint32_t waitMs   = self->mConnected ? 0 : osWaitForever;
        const osStatus_t status = osMessageQueueGet(messageQueue, &message, nullptr, waitMs);

        if (status == osOK)
        {
            if (message.operation == Operation::Stop)
            {
                break;
            }
            self->ProcessMessage(message);
            continue;
        }

        // Empty queue: CMSIS returns osErrorResource for timeout 0, osErrorTimeout for timed waits.
        if (status == osErrorTimeout || status == osErrorResource)
        {
            if (self->mConnected)
            {
                self->IdleYield();
            }
            continue;
        }

        ChipLogError(DeviceLayer, "[MQTT] service queue get failed: 0x%" PRIx32, static_cast<uint32_t>(status));
        break;
    }

    // Release socket/TLS before the service thread exits (Stop or queue-get failure).
    ChipLogDetail(DeviceLayer, "[MQTT] service thread exiting");

    self->DrainQueuedOperations(CHIP_ERROR_CANCELLED);
    LogErrorOnFailure(self->ProcessDisconnect());
    LogErrorOnFailure(self->ProcessDeinit());

    osMessageQueueDelete(static_cast<osMessageQueueId_t>(self->mMessageQueue));
    self->mMessageQueue = nullptr;
    self->mThreadId     = nullptr;
    osThreadTerminate(osThreadGetId());
}

uint32_t MqttClient::GetIdleYieldTimeoutMs()
{
    // If the idle yield timeout is not set, use the default value of 1000 ms.
    // Though this is set in the constructor, it is possible to change the value later.
    if (mConfig.idleYieldTimeoutMs == 0)
    {
        ChipLogDetail(DeviceLayer, "[MQTT] idleYieldTimeoutMs is 0 (ignored); using default 1000 ms");
        mConfig.idleYieldTimeoutMs = 1000;
    }

    const uint32_t configuredMs = mConfig.idleYieldTimeoutMs;

    // MQTTYield timeout_ms must be <= keepAliveInterval * 1000 so PINGREQ can fire in time.
    VerifyOrReturnValue(mConfig.keepAliveIntervalSec > 0, configuredMs);

    const uint32_t keepAliveMs = static_cast<uint32_t>(mConfig.keepAliveIntervalSec) * 1000u;
    return (configuredMs < keepAliveMs) ? configuredMs : keepAliveMs;
}

void MqttClient::IdleYield()
{
    VerifyOrReturn(mConnected);

    const uint32_t yieldMs = GetIdleYieldTimeoutMs();
    const int status       = MQTTYield(&mClient, static_cast<int>(yieldMs));
    if (status != SUCCESS)
    {
        ChipLogError(DeviceLayer, "[MQTT] idle yield failed: %d (session marked disconnected)", status);
        CHIP_ERROR err = ProcessDisconnect();
        if (err != CHIP_NO_ERROR)
        {
            ChipLogError(DeviceLayer, "[MQTT] idle yield disconnect failed: %" CHIP_ERROR_FORMAT, err.Format());
        }
    }
}

CHIP_ERROR MqttClient::Start()
{
    VerifyOrReturnError(mThreadId == nullptr, CHIP_NO_ERROR);

    mMessageQueue = osMessageQueueNew(kQueueSize, sizeof(ServiceMessage), nullptr);
    VerifyOrReturnError(mMessageQueue != nullptr, CHIP_ERROR_NO_MEMORY);

    osThreadAttr_t attrs = {};
    attrs.name           = "mqtt_client";
    attrs.stack_size     = kDefaultThreadStackSize;
    attrs.priority       = osPriorityAboveNormal;

    mThreadId = osThreadNew(ServiceThread, this, &attrs);
    if (mThreadId == nullptr)
    {
        osMessageQueueDelete(static_cast<osMessageQueueId_t>(mMessageQueue));
        mMessageQueue = nullptr;
        return CHIP_ERROR_INTERNAL;
    }
    ChipLogDetail(DeviceLayer, "[MQTT] client service thread started");
    return CHIP_NO_ERROR;
}

CHIP_ERROR MqttClient::Stop()
{
    VerifyOrReturnError(mMessageQueue != nullptr, CHIP_ERROR_INCORRECT_STATE);

    ServiceMessage message{};
    message.operation = Operation::Stop;
    ReturnErrorOnFailure(PostMessage(message));

    return CHIP_NO_ERROR;
}

CHIP_ERROR MqttClient::ProcessInit()
{
    VerifyOrReturnError(!mInitialized, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(mConfig.clientId != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    memset(&mClient, 0, sizeof(mClient));
    memset(&mNetwork, 0, sizeof(mNetwork));
    memset(mTxBuffer, 0, sizeof(mTxBuffer));
    memset(mRxBuffer, 0, sizeof(mRxBuffer));
    memset(&mServerIp, 0, sizeof(mServerIp));

    mNetwork.transport_type = MQTT_TRANSPORT_TCP;
    NetworkInit(&mNetwork);

    MQTTClient(&mClient, &mNetwork, mConfig.commandTimeoutMs, mTxBuffer, sizeof(mTxBuffer), mRxBuffer, sizeof(mRxBuffer));

    mInitialized  = true;
    mConnected    = false;
    sActiveClient = this;
    ChipLogDetail(DeviceLayer, "[MQTT] initialized");
    return CHIP_NO_ERROR;
}

CHIP_ERROR MqttClient::ProcessDeinit()
{
    VerifyOrReturnError(mInitialized, CHIP_ERROR_INCORRECT_STATE);

    if (mConnected)
    {
        CHIP_ERROR err = ProcessDisconnect();
        if (err != CHIP_NO_ERROR)
        {
            ChipLogError(DeviceLayer, "[MQTT] deinit disconnect failed: %" CHIP_ERROR_FORMAT, err.Format());
        }
    }

    // Defensive: free any leftover pre-connect stub after NetworkDisconnect in ProcessDisconnect.
    FreeTlsContext();
    mInitialized = false;
    if (sActiveClient == this)
    {
        sActiveClient = nullptr;
    }
    ChipLogDetail(DeviceLayer, "[MQTT] de-initialized");
    return CHIP_NO_ERROR;
}

CHIP_ERROR MqttClient::ProcessConnect()
{
    VerifyOrReturnError(mInitialized, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(!mConnected, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(mBroker.brokerIp != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(mBroker.brokerPort != 0, CHIP_ERROR_INVALID_ARGUMENT);

    mServerIp.type = SL_IPV4;
    if (sl_net_inet_addr(mBroker.brokerIp, reinterpret_cast<uint32_t *>(&mServerIp.ip.v4.value)) != SL_STATUS_OK)
    {
        ChipLogError(DeviceLayer, "[MQTT] invalid broker IP: %s", mBroker.brokerIp);
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    ChipLogDetail(DeviceLayer, "[MQTT] connecting to broker %s port %u (TLS=%s)", mBroker.brokerIp, mBroker.brokerPort,
                    mConfig.useTls ? "yes" : "no");

#if MQTT_USE_HOST_LWIP_TLS && MQTT_TLS_ENABLE
    if (mConfig.useTls)
    {
        VerifyOrReturnError(mBroker.tlsHostname != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

        FreeTlsContext();
        mNetwork.tls = static_cast<mqtt_tls_context_t *>(malloc(sizeof(mqtt_tls_context_t)));
        VerifyOrReturnError(mNetwork.tls != nullptr, CHIP_ERROR_NO_MEMORY);
        memset(mNetwork.tls, 0, sizeof(mqtt_tls_context_t));

        if (mConfig.tlsCaCert != nullptr && mConfig.tlsCaCertLen > 0)
        {
            mNetwork.tls->cert_ctx.cacert     = const_cast<uint8_t *>(mConfig.tlsCaCert);
            mNetwork.tls->cert_ctx.cacert_len = mConfig.tlsCaCertLen;
        }

        if (NetworkSetTlsHostname(&mNetwork, mBroker.tlsHostname) != 0)
        {
            ChipLogError(DeviceLayer, "MQTT invalid TLS hostname");
            FreeTlsContext();
            return CHIP_ERROR_INVALID_ARGUMENT;
        }
    }
#endif

    const int netStatus =
        NetworkConnect(&mNetwork, 0, reinterpret_cast<char *>(&mServerIp), mBroker.brokerPort, mBroker.clientPort, mConfig.useTls);
    if (netStatus != 0)
    {
        LogNetworkConnectError(netStatus);
        // Recover leftover pre-connect stub if transport did not take ownership.
        FreeTlsContext();
        return CHIP_ERROR_INTERNAL;
    }
    ChipLogDetail(DeviceLayer, "[MQTT] TCP/TLS connection established");

    MQTTPacket_connectData connectData = MQTTPacket_connectData_initializer;
    connectData.willFlag               = mConfig.willEnable ? 1 : 0;
    connectData.will.topicName.cstring = const_cast<char *>(mConfig.willTopic);
    connectData.will.message.cstring   = const_cast<char *>(mConfig.willMessage);
    connectData.will.qos               = ToPahoQos(mConfig.willQoS);
    connectData.will.retained          = mConfig.willRetained ? 1 : 0;
    connectData.MQTTVersion            = mConfig.mqttVersion;
    connectData.clientID.cstring       = const_cast<char *>(mConfig.clientId);
    connectData.username.cstring       = const_cast<char *>(mConfig.username);
    connectData.password.cstring       = const_cast<char *>(mConfig.password);
    connectData.keepAliveInterval      = mConfig.keepAliveIntervalSec;
    connectData.cleansession           = mConfig.cleanSession ? 1 : 0;

    const int mqttStatus = MQTTConnect(&mClient, &connectData);
    if (mqttStatus != SUCCESS)
    {
        ChipLogError(DeviceLayer, "[MQTT] connect failed: %d", mqttStatus);
        // Transport frees the live TLS context; FreeTlsContext is stub-only if still set.
        NetworkDisconnect(&mNetwork);
        FreeTlsContext();
        return MapPahoStatus(mqttStatus);
    }

    mConnected = true;
    ChipLogDetail(DeviceLayer, "[MQTT] connected");
    return CHIP_NO_ERROR;
}

CHIP_ERROR MqttClient::ProcessDisconnect()
{
    VerifyOrReturnError(mInitialized, CHIP_ERROR_INCORRECT_STATE);

    if (mConnected)
    {
        const int status = MQTTDisconnect(&mClient);
        if (status != SUCCESS)
        {
            ChipLogError(DeviceLayer, "[MQTT] disconnect failed: %d", status);
        }
        mConnected = false;
    }

    // NetworkDisconnect frees the live TLS context; FreeTlsContext clears any leftover stub.
    NetworkDisconnect(&mNetwork);
    FreeTlsContext();
    ChipLogDetail(DeviceLayer, "[MQTT] disconnected");
    return CHIP_NO_ERROR;
}

CHIP_ERROR MqttClient::ProcessSubscribe(const ServiceMessage & message)
{
    VerifyOrReturnError(mConnected, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(message.topic != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    const int status = MQTTSubscribe(&mClient, const_cast<char *>(message.topic), ToPahoQos(message.qos), PahoMessageHandler);
    if (status != SUCCESS)
    {
        ChipLogError(DeviceLayer, "[MQTT] subscribe failed: %d", status);
        return MapPahoStatus(status);
    }

    ChipLogDetail(DeviceLayer, "[MQTT] subscribed to %s", message.topic);
    return CHIP_NO_ERROR;
}

CHIP_ERROR MqttClient::ProcessUnsubscribe(const ServiceMessage & message)
{
    VerifyOrReturnError(mConnected, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(message.topic != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    const int status = MQTTUnsubscribe(&mClient, message.topic);
    if (status != SUCCESS)
    {
        ChipLogError(DeviceLayer, "[MQTT] unsubscribe failed: %d", status);
        return MapPahoStatus(status);
    }

    ChipLogDetail(DeviceLayer, "[MQTT] unsubscribed from %s", message.topic);
    return CHIP_NO_ERROR;
}

CHIP_ERROR MqttClient::ProcessPublish(const ServiceMessage & message)
{
    VerifyOrReturnError(mConnected, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(message.topic != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    MQTTMessage mqttMessage = {};
    mqttMessage.qos         = ToPahoQos(message.qos);
    mqttMessage.retained    = message.retained ? 1 : 0;
    mqttMessage.dup         = 0;
    mqttMessage.payload     = const_cast<uint8_t *>(message.payload.data());
    mqttMessage.payloadlen  = message.payload.size();

    const int status = MQTTPublish(&mClient, message.topic, &mqttMessage);
    if (status != SUCCESS)
    {
        ChipLogError(DeviceLayer, "[MQTT] publish failed: %d", status);
        return MapPahoStatus(status);
    }

    ChipLogDetail(DeviceLayer, "[MQTT] published to %s", message.topic);
    return CHIP_NO_ERROR;
}

CHIP_ERROR MqttClient::ProcessYield(const ServiceMessage & message)
{
    VerifyOrReturnError(mConnected, CHIP_ERROR_INCORRECT_STATE);

    const int status = MQTTYield(&mClient, static_cast<int>(message.yieldTimeoutMs));
    if (status != SUCCESS)
    {
        ChipLogError(DeviceLayer, "[MQTT] yield failed: %d", status);
        CHIP_ERROR err = ProcessDisconnect();
        if (err != CHIP_NO_ERROR)
        {
            ChipLogError(DeviceLayer, "[MQTT] yield disconnect failed: %" CHIP_ERROR_FORMAT, err.Format());
        }
        return MapPahoStatus(status);
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR MqttClient::SetConfig(const MqttClientConfig & config)
{
    VerifyOrReturnError(!mConnected, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(config.clientId != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    mConfig = config;
    return CHIP_NO_ERROR;
}

CHIP_ERROR MqttClient::SetBroker(const MqttBroker & broker)
{
    VerifyOrReturnError(!mConnected, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(broker.brokerIp != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(broker.brokerPort != 0, CHIP_ERROR_INVALID_ARGUMENT);

    mBroker = broker;
    return CHIP_NO_ERROR;
}

CHIP_ERROR MqttClient::Init(MqttOperationCallback callback, void * context)
{
    VerifyOrReturnError(IsRunning(), CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(!mConnected, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(mConfig.clientId != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    ServiceMessage message{};
    message.operation = Operation::Init;
    message.callback  = callback;
    message.context   = context;
    return PostMessage(message);
}

CHIP_ERROR MqttClient::Init(const MqttClientConfig & config, MqttOperationCallback callback, void * context)
{
    ReturnErrorOnFailure(SetConfig(config));
    return Init(callback, context);
}

CHIP_ERROR MqttClient::Deinit(MqttOperationCallback callback, void * context)
{
    VerifyOrReturnError(IsRunning(), CHIP_ERROR_INCORRECT_STATE);
    if (!mInitialized)
    {
        if (callback != nullptr)
        {
            callback(CHIP_NO_ERROR, context);
        }
        return CHIP_NO_ERROR;
    }

    ServiceMessage message{};
    message.operation = Operation::Deinit;
    message.callback  = callback;
    message.context   = context;
    return PostMessage(message);
}

CHIP_ERROR MqttClient::Connect(MqttOperationCallback callback, void * context)
{
    VerifyOrReturnError(IsRunning(), CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(!mConnected, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(mBroker.brokerIp != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(mBroker.brokerPort != 0, CHIP_ERROR_INVALID_ARGUMENT);

    ServiceMessage message{};
    message.operation = Operation::Connect;
    message.callback  = callback;
    message.context   = context;
    return PostMessage(message);
}

CHIP_ERROR MqttClient::Connect(const MqttBroker & broker, MqttOperationCallback callback, void * context)
{
    ReturnErrorOnFailure(SetBroker(broker));
    return Connect(callback, context);
}

CHIP_ERROR MqttClient::Disconnect(MqttOperationCallback callback, void * context)
{
    VerifyOrReturnError(IsRunning(), CHIP_ERROR_INCORRECT_STATE);

    ServiceMessage message{};
    message.operation = Operation::Disconnect;
    message.callback  = callback;
    message.context   = context;
    return PostMessage(message);
}

CHIP_ERROR MqttClient::Subscribe(const char * topic, MqttQoS qos, MqttOperationCallback callback, void * context)
{
    VerifyOrReturnError(IsRunning(), CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(topic != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    ServiceMessage message{};
    message.operation = Operation::Subscribe;
    message.callback  = callback;
    message.context   = context;
    message.topic     = topic;
    message.qos       = qos;
    return PostMessage(message);
}

CHIP_ERROR MqttClient::Subscribe(const char * topic, MqttOperationCallback callback, void * context)
{
    return Subscribe(topic, mConfig.subQoS, callback, context);
}

CHIP_ERROR MqttClient::Unsubscribe(const char * topic, MqttOperationCallback callback, void * context)
{
    VerifyOrReturnError(IsRunning(), CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(topic != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    ServiceMessage message{};
    message.operation = Operation::Unsubscribe;
    message.callback  = callback;
    message.context   = context;
    message.topic     = topic;
    return PostMessage(message);
}

CHIP_ERROR MqttClient::Publish(const char * topic, ByteSpan payload, MqttQoS qos, bool retained, MqttOperationCallback callback,
                               void * context)
{
    VerifyOrReturnError(IsRunning(), CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(topic != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    ServiceMessage message{};
    message.operation = Operation::Publish;
    message.callback  = callback;
    message.context   = context;
    message.topic     = topic;
    message.payload   = payload;
    message.qos       = qos;
    message.retained  = retained;
    return PostMessage(message);
}

CHIP_ERROR MqttClient::Publish(const char * topic, ByteSpan payload, bool retained, MqttOperationCallback callback, void * context)
{
    return Publish(topic, payload, mConfig.pubQoS, retained, callback, context);
}

CHIP_ERROR MqttClient::Yield(uint32_t timeoutMs, MqttOperationCallback callback, void * context)
{
    VerifyOrReturnError(IsRunning(), CHIP_ERROR_INCORRECT_STATE);

    ServiceMessage message{};
    message.operation      = Operation::Yield;
    message.callback       = callback;
    message.context        = context;
    message.yieldTimeoutMs = timeoutMs;
    return PostMessage(message);
}

} // namespace Silabs
} // namespace DeviceLayer
} // namespace chip
