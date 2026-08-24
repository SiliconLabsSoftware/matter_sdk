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

bool MqttClient::IsBusy() const
{
    return mBusy;
}

void MqttClient::SetSubscriptionCallback(MqttSubscriptionCallback callback, void * context)
{
    mMessageCallback        = callback;
    mMessageCallbackContext = context;
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
    ChipLogProgress(DeviceLayer, "MQTT message received on topic: %s", topic != nullptr ? topic : "unknown");

    const ByteSpan payload(static_cast<const uint8_t *>(md->message->payload), md->message->payloadlen);
    if (self->mMessageCallback != nullptr)
    {
        self->mMessageCallback(topic, payload, self->mMessageCallbackContext);
    }
    ChipLogProgress(DeviceLayer, "MQTT message: %.*s", static_cast<int>(payload.size()),
                    reinterpret_cast<const char *>(payload.data()));
    free(topic);
    topic = nullptr;
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

void MqttClient::ServiceThread(void * arg)
{
    auto * self = static_cast<MqttClient *>(arg);
    VerifyOrReturn(self != nullptr);

    auto eventFlags = static_cast<osEventFlagsId_t>(self->mEventFlags);
    while (true)
    {
        uint32_t events = osEventFlagsWait(eventFlags, kEventOperation | kEventStop, osFlagsWaitAny, osWaitForever);
        if (events & osFlagsError)
        {
            ChipLogError(DeviceLayer, "MQTT service event wait failed: 0x%lx", static_cast<unsigned long>(events));
            break;
        }
        if (events & kEventStop)
        {
            break;
        }
        if (events & kEventOperation)
        {
            self->ProcessOperation();
        }
    }

    osEventFlagsDelete(static_cast<osEventFlagsId_t>(self->mEventFlags));
    self->mEventFlags = nullptr;
    self->mThreadId   = nullptr;
    osThreadTerminate(osThreadGetId());
}

CHIP_ERROR MqttClient::Start()
{
    VerifyOrReturnError(mThreadId == nullptr, CHIP_NO_ERROR);

    mEventFlags = osEventFlagsNew(nullptr);
    VerifyOrReturnError(mEventFlags != nullptr, CHIP_ERROR_INTERNAL);

    osThreadAttr_t attrs = {};
    attrs.name           = "mqtt_client";
    attrs.stack_size     = kDefaultThreadStackSize;
    attrs.priority       = osPriorityAboveNormal;

    mThreadId = osThreadNew(ServiceThread, this, &attrs);
    if (mThreadId == nullptr)
    {
        osEventFlagsDelete(static_cast<osEventFlagsId_t>(mEventFlags));
        mEventFlags = nullptr;
        return CHIP_ERROR_INTERNAL;
    }
    ChipLogProgress(DeviceLayer, "MQTT client service thread started");
    return CHIP_NO_ERROR;
}

CHIP_ERROR MqttClient::Stop()
{
    VerifyOrReturnError(mEventFlags != nullptr, CHIP_ERROR_INCORRECT_STATE);
    // TODO: should we terminate the thread if it is busy?
    VerifyOrReturnError(!mBusy, CHIP_ERROR_BUSY);
    osEventFlagsSet(static_cast<osEventFlagsId_t>(mEventFlags), kEventStop);
    ChipLogProgress(DeviceLayer, "MQTT client service thread stopped");
    return CHIP_NO_ERROR;
}

CHIP_ERROR MqttClient::QueueOperation(Operation operation, MqttOperationCallback callback, void * context)
{
    VerifyOrReturnError(IsRunning(), CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(!mBusy, CHIP_ERROR_BUSY);

    mPendingOperation    = operation;
    mUserCallback        = callback;
    mUserCallbackContext = context;
    mBusy                = true;
    osEventFlagsSet(static_cast<osEventFlagsId_t>(mEventFlags), kEventOperation);
    return CHIP_NO_ERROR;
}

void MqttClient::CompleteOperation(CHIP_ERROR error)
{
    MqttOperationCallback callback = mUserCallback;
    void * context                 = mUserCallbackContext;

    mPendingOperation    = Operation::None;
    mUserCallback        = nullptr;
    mUserCallbackContext = nullptr;
    mPendingTopic        = nullptr;
    mPendingPayload      = ByteSpan();
    mBusy                = false;

    if (callback != nullptr)
    {
        callback(error, context);
    }
}

void MqttClient::ProcessOperation()
{
    CHIP_ERROR err = CHIP_ERROR_INTERNAL;

    switch (mPendingOperation)
    {
    case Operation::Init:
        err = ProcessInit();
        break;
    case Operation::Deinit:
        err = ProcessDeinit();
        break;
    case Operation::Connect:
        err = ProcessConnect();
        break;
    case Operation::Disconnect:
        err = ProcessDisconnect();
        break;
    case Operation::Subscribe:
        err = ProcessSubscribe();
        break;
    case Operation::Unsubscribe:
        err = ProcessUnsubscribe();
        break;
    case Operation::Publish:
        err = ProcessPublish();
        break;
    case Operation::Yield:
        err = ProcessYield();
        break;
    case Operation::None:
    default:
        err = CHIP_ERROR_INCORRECT_STATE;
        break;
    }

    CompleteOperation(err);
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
    ChipLogProgress(DeviceLayer, "MQTT client initialized");
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
            ChipLogError(DeviceLayer, "MQTT Deinit disconnect failed: %" CHIP_ERROR_FORMAT, err.Format());
        }
    }

    FreeTlsContext();
    mInitialized = false;
    if (sActiveClient == this)
    {
        sActiveClient = nullptr;
    }
    ChipLogProgress(DeviceLayer, "MQTT client deinitialized");
    return CHIP_NO_ERROR;
}

CHIP_ERROR MqttClient::ProcessConnect()
{
    VerifyOrReturnError(mInitialized, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(!mConnected, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(mPendingBroker.brokerIp != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(mPendingBroker.brokerPort != 0, CHIP_ERROR_INVALID_ARGUMENT);

    mServerIp.type = SL_IPV4;
    if (sl_net_inet_addr(mPendingBroker.brokerIp, reinterpret_cast<uint32_t *>(&mServerIp.ip.v4.value)) != SL_STATUS_OK)
    {
        ChipLogError(DeviceLayer, "MQTT invalid broker IP: %s", mPendingBroker.brokerIp);
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    ChipLogProgress(DeviceLayer, "MQTT connecting to broker %s port %u (TLS=%s)", mPendingBroker.brokerIp,
                    mPendingBroker.brokerPort, mConfig.useTls ? "yes" : "no");

#if MQTT_USE_HOST_LWIP_TLS && MQTT_TLS_ENABLE
    if (mConfig.useTls)
    {
        VerifyOrReturnError(mPendingBroker.tlsHostname != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

        FreeTlsContext();
        mNetwork.tls = static_cast<mqtt_tls_context_t *>(malloc(sizeof(mqtt_tls_context_t)));
        VerifyOrReturnError(mNetwork.tls != nullptr, CHIP_ERROR_NO_MEMORY);
        memset(mNetwork.tls, 0, sizeof(mqtt_tls_context_t));

        if (mConfig.tlsCaCert != nullptr && mConfig.tlsCaCertLen > 0)
        {
            mNetwork.tls->cert_ctx.cacert     = const_cast<uint8_t *>(mConfig.tlsCaCert);
            mNetwork.tls->cert_ctx.cacert_len = mConfig.tlsCaCertLen;
        }

        if (NetworkSetTlsHostname(&mNetwork, mPendingBroker.tlsHostname) != 0)
        {
            ChipLogError(DeviceLayer, "MQTT invalid TLS hostname");
            FreeTlsContext();
            return CHIP_ERROR_INVALID_ARGUMENT;
        }
    }
#endif

    const int netStatus = NetworkConnect(&mNetwork, 0, reinterpret_cast<char *>(&mServerIp), mPendingBroker.brokerPort,
                                         mPendingBroker.clientPort, mConfig.useTls);
    if (netStatus != 0)
    {
        LogNetworkConnectError(netStatus);
        FreeTlsContext();
        return CHIP_ERROR_INTERNAL;
    }
    ChipLogProgress(DeviceLayer, "MQTT TCP/TLS connection established");

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
        ChipLogError(DeviceLayer, "MQTT CONNECT failed: %d", mqttStatus);
        NetworkDisconnect(&mNetwork);
        FreeTlsContext();
        return MapPahoStatus(mqttStatus);
    }

    mConnected = true;
    ChipLogProgress(DeviceLayer, "MQTT connected");
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
            ChipLogError(DeviceLayer, "MQTT DISCONNECT failed: %d", status);
        }
        mConnected = false;
    }

    NetworkDisconnect(&mNetwork);
    FreeTlsContext();
    ChipLogProgress(DeviceLayer, "MQTT disconnected");
    return CHIP_NO_ERROR;
}

CHIP_ERROR MqttClient::ProcessSubscribe()
{
    VerifyOrReturnError(mConnected, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(mPendingTopic != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    const int status = MQTTSubscribe(&mClient, const_cast<char *>(mPendingTopic), ToPahoQos(mPendingQos), PahoMessageHandler);
    if (status != SUCCESS)
    {
        ChipLogError(DeviceLayer, "MQTT SUBSCRIBE failed: %d", status);
        return MapPahoStatus(status);
    }

    ChipLogProgress(DeviceLayer, "MQTT subscribed to %s", mPendingTopic);
    return CHIP_NO_ERROR;
}

CHIP_ERROR MqttClient::ProcessUnsubscribe()
{
    VerifyOrReturnError(mConnected, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(mPendingTopic != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    const int status = MQTTUnsubscribe(&mClient, mPendingTopic);
    if (status != SUCCESS)
    {
        ChipLogError(DeviceLayer, "MQTT UNSUBSCRIBE failed: %d", status);
        return MapPahoStatus(status);
    }

    ChipLogProgress(DeviceLayer, "MQTT unsubscribed from %s", mPendingTopic);
    return CHIP_NO_ERROR;
}

CHIP_ERROR MqttClient::ProcessPublish()
{
    VerifyOrReturnError(mConnected, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(mPendingTopic != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    MQTTMessage message = {};
    message.qos         = ToPahoQos(mPendingQos);
    message.retained    = mPendingRetained ? 1 : 0;
    message.dup         = 0;
    message.payload     = const_cast<uint8_t *>(mPendingPayload.data());
    message.payloadlen  = mPendingPayload.size();

    const int status = MQTTPublish(&mClient, mPendingTopic, &message);
    if (status != SUCCESS)
    {
        ChipLogError(DeviceLayer, "MQTT PUBLISH failed: %d", status);
        return MapPahoStatus(status);
    }

    ChipLogProgress(DeviceLayer, "MQTT published to %s", mPendingTopic);
    return CHIP_NO_ERROR;
}

CHIP_ERROR MqttClient::ProcessYield()
{
    VerifyOrReturnError(mConnected, CHIP_ERROR_INCORRECT_STATE);

    const int status = MQTTYield(&mClient, static_cast<int>(mPendingYieldTimeoutMs));
    if (status != SUCCESS)
    {
        ChipLogError(DeviceLayer, "MQTT Yield failed: %d", status);
        return MapPahoStatus(status);
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR MqttClient::Init(const MqttClientConfig & config, MqttOperationCallback callback, void * context)
{
    VerifyOrReturnError(IsRunning(), CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(!mBusy, CHIP_ERROR_BUSY);
    VerifyOrReturnError(!mInitialized, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(config.clientId != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    mConfig = config;
    return QueueOperation(Operation::Init, callback, context);
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
    return QueueOperation(Operation::Deinit, callback, context);
}

CHIP_ERROR MqttClient::Connect(const MqttBroker & broker, MqttOperationCallback callback, void * context)
{
    VerifyOrReturnError(IsRunning(), CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(mInitialized, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(!mBusy, CHIP_ERROR_BUSY);
    VerifyOrReturnError(broker.brokerIp != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    mPendingBroker = broker;
    return QueueOperation(Operation::Connect, callback, context);
}

CHIP_ERROR MqttClient::Disconnect(MqttOperationCallback callback, void * context)
{
    VerifyOrReturnError(IsRunning(), CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(mInitialized, CHIP_ERROR_INCORRECT_STATE);
    return QueueOperation(Operation::Disconnect, callback, context);
}

CHIP_ERROR MqttClient::Subscribe(const char * topic, MqttQoS qos, MqttOperationCallback callback, void * context)
{
    VerifyOrReturnError(IsRunning(), CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(mInitialized, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(!mBusy, CHIP_ERROR_BUSY);
    VerifyOrReturnError(topic != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    mPendingTopic = topic;
    mPendingQos   = qos;
    return QueueOperation(Operation::Subscribe, callback, context);
}

CHIP_ERROR MqttClient::Unsubscribe(const char * topic, MqttOperationCallback callback, void * context)
{
    VerifyOrReturnError(IsRunning(), CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(mInitialized, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(!mBusy, CHIP_ERROR_BUSY);
    VerifyOrReturnError(topic != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    mPendingTopic = topic;
    return QueueOperation(Operation::Unsubscribe, callback, context);
}

CHIP_ERROR MqttClient::Publish(const char * topic, ByteSpan payload, MqttQoS qos, bool retained, MqttOperationCallback callback,
                               void * context)
{
    VerifyOrReturnError(IsRunning(), CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(mInitialized, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(!mBusy, CHIP_ERROR_BUSY);
    VerifyOrReturnError(topic != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    mPendingTopic    = topic;
    mPendingPayload  = payload;
    mPendingQos      = qos;
    mPendingRetained = retained;
    return QueueOperation(Operation::Publish, callback, context);
}

CHIP_ERROR MqttClient::Yield(uint32_t timeoutMs, MqttOperationCallback callback, void * context)
{
    VerifyOrReturnError(IsRunning(), CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(mInitialized, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(!mBusy, CHIP_ERROR_BUSY);

    mPendingYieldTimeoutMs = timeoutMs;
    return QueueOperation(Operation::Yield, callback, context);
}

} // namespace Silabs
} // namespace DeviceLayer
} // namespace chip
