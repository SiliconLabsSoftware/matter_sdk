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

#include "matter_service.h"

#include <lib/core/CHIPError.h>
#include <lib/support/Span.h>

#include <cstddef>
#include <cstdint>

#include "sl_status.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "MQTTClient.h"
#include "sl_net.h"
#ifdef __cplusplus
}
#endif

// MQTT_USE_HOST_LWIP_TLS to use the host LwIP TLS context
// making it MQTTs client else it will be a MQTT client
#ifndef MQTT_USE_HOST_LWIP_TLS
#define MQTT_USE_HOST_LWIP_TLS 0
#endif // MQTT_USE_HOST_LWIP_TLS

namespace chip {
namespace DeviceLayer {
namespace Silabs {

/**
 * @brief MQTT quality-of-service levels (maps to Paho QOS0 / QOS1 / QOS2).
 */
enum class MqttQoS : uint8_t
{
    QoS0 = 0,
    QoS1 = 1,
    QoS2 = 2,
};

/**
 * @brief Configuration for @ref MqttClient::Init.
 *
 * When @p useTls is true and @p tlsCaCert is non-null, Connect installs the PEM into the
 * host LwIP TLS context before NetworkConnect.
 */
struct MqttClientConfig
{
    bool useTls                   = true;
    const char * clientId         = nullptr;
    const char * username         = nullptr;
    const char * password         = nullptr;
    uint16_t keepAliveIntervalSec = 100;
    unsigned int commandTimeoutMs = 20000;
    uint8_t mqttVersion           = 4;
    bool cleanSession             = true;
    uint32_t idleYieldTimeoutMs   = 1000; // This is the timeout for the idle yield operation.

    bool willEnable          = false;
    const char * willTopic   = nullptr;
    const char * willMessage = nullptr;
    // Defaults used when Subscribe / Publish omit an explicit QoS.
    MqttQoS willQoS   = MqttQoS::QoS1; ///< Last Will QoS when willEnable is true.
    bool willRetained = false;
    MqttQoS pubQoS    = MqttQoS::QoS1; ///< Default Publish QoS.
    MqttQoS subQoS    = MqttQoS::QoS1; ///< Default Subscribe QoS.

    /** PEM bytes for host mbedTLS CA chain. nullptr = skip CA install. */
    const uint8_t * tlsCaCert = nullptr;
    size_t tlsCaCertLen       = 0;
};

/**
 * @brief Broker destination for @ref MqttClient::Connect.
 *
 * Provide either @p brokerHostname (DNS) or @p brokerIp (literal). When @p brokerHostname is set,
 * Connect resolves it into mServerIp and uses the hostname as @p tlsHostname.
 */
struct MqttBroker
{
    const char * brokerHostname = nullptr; ///< Optional DNS name. When set, resolves into mServerIp / tlsHostname.
    const char * brokerIp       = nullptr; ///< IPv4 string for NetworkConnect. Required if brokerHostname is null.
    const char * tlsHostname    = nullptr; ///< SNI / cert verify name when TLS is enabled.
    uint16_t brokerPort         = 0;       ///< Broker port (1883 or 8883 typical).
    uint16_t clientPort         = 0;       ///< Local source port.
};

/**
 * @brief Completion callback for a queued MqttClient operation.
 *
 * Invoked on the MqttClient service thread when the operation finishes.
 */
using MqttOperationCallback = void (*)(CHIP_ERROR result, void * context);

/**
 * @brief Incoming publish handler.
 *
 * Invoked on the MqttClient service thread from Yield / Subscribe delivery.
 * @p topic and @p payload are valid only for the duration of the call.
 */
using MqttSubscriptionCallback = void (*)(const char * topic, ByteSpan payload, void * context);

/**
 * @brief Host LwIP Paho MQTT client usable from Matter C++ code.
 *
 * Lifecycle: Start → Init → Connect → Subscribe/Publish → Disconnect → Deinit → Stop.
 * Operations are queued to the service thread and report completion via @ref MqttOperationCallback.
 * Disconnect / Deinit cancel any still-queued operations with @ref CHIP_ERROR_CANCELLED.
 * While connected and idle, the service thread auto-runs MQTTYield for keepalive and inbound publishes.
 *
 * @ref MqttClientConfig and @ref MqttBroker are instance state shared by all operations.
 * They may only change while disconnected (before Connect, or after Disconnect).
 */
class MqttClient : public MatterService
{
public:
    /**
     * @brief Update client config. Allowed only while disconnected.
     *
     * Pointer fields must remain valid for as long as the config is in use (through Connect / session).
     */
    CHIP_ERROR SetConfig(const MqttClientConfig & config);

    /**
     * @brief Update broker destination. Allowed only while disconnected.
     *
     * Pointer fields must remain valid until Connect completes or the broker is replaced after Disconnect.
     */
    CHIP_ERROR SetBroker(const MqttBroker & broker);

    /**
     * @brief Queue client buffer / Network / MQTTClient setup on the service thread.
     *
     * Applies @p config via @ref SetConfig (disconnected only), then queues Init.
     */
    CHIP_ERROR Init(const MqttClientConfig & config, MqttOperationCallback callback, void * context = nullptr);

    /**
     * @brief Queue Init using the config last set by @ref SetConfig / @ref Init.
     */
    CHIP_ERROR Init(MqttOperationCallback callback, void * context = nullptr);

    /**
     * @brief Queue teardown of an initialized (and preferably disconnected) client.
     *
     * Cancels any still-queued operations before teardown.
     */
    CHIP_ERROR Deinit(MqttOperationCallback callback, void * context = nullptr);

    CHIP_ERROR Start() override;
    CHIP_ERROR Stop() override;
    bool IsRunning() const override;
    bool IsBusy() const;
    bool IsInitialized() const { return mInitialized; }
    bool IsConnected() const { return mConnected; }

    /**
     * @brief Set handler for subscribed publishes. Safe to call before Connect.
     */
    void SetSubscriptionCallback(MqttSubscriptionCallback callback, void * context = nullptr);

    /**
     * @brief TCP/TLS NetworkConnect + MQTT CONNECT using @p broker.
     *
     * Applies @p broker via @ref SetBroker (disconnected only), then queues Connect.
     */
    CHIP_ERROR Connect(const MqttBroker & broker, MqttOperationCallback callback, void * context = nullptr);

    /**
     * @brief Queue Connect using the broker last set by @ref SetBroker / @ref Connect.
     */
    CHIP_ERROR Connect(MqttOperationCallback callback, void * context = nullptr);

    /**
     * @brief Queue MQTT / network disconnect.
     *
     * Cancels any still-queued operations before disconnect.
     */
    CHIP_ERROR Disconnect(MqttOperationCallback callback, void * context = nullptr);

    /**
     * @brief Subscribe with the instance message callback (@ref SetSubscriptionCallback).
     *
     * @p topic must remain valid until the operation completes or is cancelled.
     */
    CHIP_ERROR Subscribe(const char * topic, MqttQoS qos, MqttOperationCallback callback, void * context = nullptr);

    /**
     * @brief Subscribe using @ref MqttClientConfig::subQoS from Init.
     */
    CHIP_ERROR Subscribe(const char * topic, MqttOperationCallback callback, void * context = nullptr);

    CHIP_ERROR Unsubscribe(const char * topic, MqttOperationCallback callback, void * context = nullptr);

    /**
     * @brief Publish @p payload to @p topic.
     *
     * @p topic and @p payload must remain valid until the operation completes or is cancelled.
     */
    CHIP_ERROR Publish(const char * topic, ByteSpan payload, MqttQoS qos, bool retained, MqttOperationCallback callback,
                       void * context = nullptr);

    /**
     * @brief Publish using @ref MqttClientConfig::pubQoS from Init.
     */
    CHIP_ERROR Publish(const char * topic, ByteSpan payload, bool retained, MqttOperationCallback callback,
                       void * context = nullptr);

    /**
     * @brief Queue a one-shot MQTTYield (optional; idle auto-yield covers keepalive/receive).
     */
    CHIP_ERROR Yield(uint32_t timeoutMs, MqttOperationCallback callback, void * context = nullptr);

private:
    static constexpr size_t kTxBufferSize           = 1024;
    static constexpr size_t kRxBufferSize           = 1024;
    static constexpr size_t kDefaultThreadStackSize = 4 * 1024;
    static constexpr size_t kQueueSize              = 4;

    enum class Operation : uint8_t
    {
        Stop,
        Init,
        Deinit,
        Connect,
        Disconnect,
        Subscribe,
        Unsubscribe,
        Publish,
        Yield,
    };

    struct ServiceMessage
    {
        Operation operation            = Operation::Stop;
        MqttOperationCallback callback = nullptr;
        void * context                 = nullptr;
        const char * topic             = nullptr;
        ByteSpan payload               = {};
        MqttQoS qos                    = MqttQoS::QoS0;
        bool retained                  = false;
        uint32_t yieldTimeoutMs        = 0;
    };

    static void ServiceThread(void * arg);
    static void PahoMessageHandler(MessageData * md);
    // Status mappers are API-specific: the same integer can mean different things
    // across NetworkConnect / MQTTConnect / MQTTSubscribe / other client calls.
    static CHIP_ERROR MapNetworkConnectStatus(int status);
    static CHIP_ERROR MapMqttConnectStatus(int status);
    static CHIP_ERROR MapMqttSubscribeStatus(int status);
    static CHIP_ERROR MapMqttReturnCode(int status);
    static enum QoS ToPahoQos(MqttQoS qos);
    static void InvokeCallback(const ServiceMessage & message, CHIP_ERROR error);

    CHIP_ERROR PostMessage(const ServiceMessage & message);
    void ProcessMessage(const ServiceMessage & message);
    void DrainQueuedOperations(CHIP_ERROR reason);
    void IdleYield();
    uint32_t GetIdleYieldTimeoutMs();
    bool HasQueuedMessages() const;

    CHIP_ERROR ProcessInit();
    CHIP_ERROR ProcessDeinit();
    CHIP_ERROR ProcessConnect();
    CHIP_ERROR ProcessDisconnect();
    CHIP_ERROR ProcessSubscribe(const ServiceMessage & message);
    CHIP_ERROR ProcessUnsubscribe(const ServiceMessage & message);
    CHIP_ERROR ProcessPublish(const ServiceMessage & message);
    CHIP_ERROR ProcessYield(const ServiceMessage & message);
    CHIP_ERROR ResolveBrokerHostname();

    void FreeTlsContext();

    MqttClientConfig mConfig{};
    MqttBroker mBroker{};

    Client mClient{};
    Network mNetwork{};
    uint8_t mTxBuffer[kTxBufferSize] = { 0 };
    uint8_t mRxBuffer[kRxBufferSize] = { 0 };
    sl_ip_address_t mServerIp{};

    void * mThreadId     = nullptr; // osThreadId_t
    void * mMessageQueue = nullptr; // osMessageQueueId_t

    bool mInitialized = false;
    bool mConnected   = false;

    MqttSubscriptionCallback mMessageCallback = nullptr;
    void * mMessageCallbackContext            = nullptr;

    static MqttClient * sActiveClient;
};

} // namespace Silabs
} // namespace DeviceLayer
} // namespace chip
