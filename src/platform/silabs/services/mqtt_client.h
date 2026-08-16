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

#ifdef __cplusplus
extern "C" {
#endif
#include "MQTTClient.h"
#include "sl_net.h"
#include "sl_status.h"
#ifdef __cplusplus
}
#endif

namespace chip {
namespace DeviceLayer {
namespace Silabs {

/**
 * @brief MQTT quality-of-service levels (maps to Paho QOS0 / QOS1 / QOS2).
 */
enum class MqttQos : uint8_t
{
    AtMostOnce  = 0,
    AtLeastOnce = 1,
    ExactlyOnce = 2,
};

/**
 * @brief Configuration for @ref MqttClient::Init.
 *
 * When @p useTls is true and @p tlsCaCert is non-null, Connect installs the PEM into the
 * host LwIP TLS context before NetworkConnect.
 */
struct MqttClientConfig
{
    bool useTls                     = true;
    const char * clientId           = nullptr;
    const char * username           = nullptr;
    const char * password           = nullptr;
    uint16_t keepAliveIntervalSec   = 100;
    unsigned int commandTimeoutMs   = 20000;
    uint8_t mqttVersion             = 4;
    bool cleanSession               = true;

    bool willEnable                 = false;
    const char * willTopic          = nullptr;
    const char * willMessage        = nullptr;
    MqttQos willQos                 = MqttQos::AtLeastOnce;
    bool willRetained               = false;

    /** PEM bytes for host mbedTLS CA chain. nullptr = skip CA install. */
    const uint8_t * tlsCaCert = nullptr;
    size_t tlsCaCertLen       = 0;
};

/**
 * @brief Broker destination for @ref MqttClient::Connect.
 */
struct MqttBroker
{
    const char * brokerIp     = nullptr; ///< IPv4 string for sl_net_inet_addr. Must not be nullptr.
    const char * tlsHostname  = nullptr; ///< SNI / cert verify name when TLS is enabled.
    uint16_t brokerPort       = 0;       ///< Broker port (1883 or 8883 typical).
    uint16_t clientPort       = 0;       ///< Local source port.
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
using MqttMessageCallback = void (*)(const char * topic, ByteSpan payload, void * context);

/**
 * @brief Host LwIP Paho MQTT client usable from Matter C++ code.
 *
 * Lifecycle: Start → Init → Connect → Subscribe/Publish/Yield → Disconnect → Deinit → Stop.
 * Operations are queued to the service thread and report completion via @ref MqttOperationCallback.
 */
class MqttClient : public MatterService
{
public:
    /**
     * @brief Queue client buffer / Network / MQTTClient setup on the service thread.
     */
    CHIP_ERROR Init(const MqttClientConfig & config, MqttOperationCallback callback, void * context = nullptr);

    /**
     * @brief Queue teardown of an initialized (and preferably disconnected) client.
     */
    CHIP_ERROR Deinit(MqttOperationCallback callback, void * context = nullptr);

    CHIP_ERROR Start() override;
    CHIP_ERROR Stop() override;
    bool IsRunning() const override;
    bool IsBusy() const;

    /**
     * @brief Set handler for subscribed publishes. Safe to call before Connect.
     */
    void SetMessageCallback(MqttMessageCallback callback, void * context = nullptr);

    /**
     * @brief TCP/TLS NetworkConnect + MQTT CONNECT.
     *
     * @p broker string pointers must remain valid until the operation completes.
     */
    CHIP_ERROR Connect(const MqttBroker & broker, MqttOperationCallback callback, void * context = nullptr);

    CHIP_ERROR Disconnect(MqttOperationCallback callback, void * context = nullptr);

    /**
     * @brief Subscribe with the instance message callback (@ref SetMessageCallback).
     *
     * @p topic must remain valid until the operation completes.
     */
    CHIP_ERROR Subscribe(const char * topic, MqttQos qos, MqttOperationCallback callback, void * context = nullptr);

    CHIP_ERROR Unsubscribe(const char * topic, MqttOperationCallback callback, void * context = nullptr);

    /**
     * @brief Publish @p payload to @p topic.
     *
     * @p topic and @p payload must remain valid until the operation completes.
     */
    CHIP_ERROR Publish(const char * topic, ByteSpan payload, MqttQos qos, bool retained, MqttOperationCallback callback,
                       void * context = nullptr);

    /**
     * @brief Run MQTTYield for up to @p timeoutMs (delivers messages via @ref SetMessageCallback).
     */
    CHIP_ERROR Yield(uint32_t timeoutMs, MqttOperationCallback callback, void * context = nullptr);

private:
    static constexpr size_t kTxBufferSize           = 1500;
    static constexpr size_t kRxBufferSize           = 1500;
    static constexpr size_t kDefaultThreadStackSize = 8 * 1024;

    enum class Operation : uint8_t
    {
        None,
        Init,
        Deinit,
        Connect,
        Disconnect,
        Subscribe,
        Unsubscribe,
        Publish,
        Yield,
    };

    static constexpr uint32_t kEventOperation = 1u << 0;
    static constexpr uint32_t kEventStop      = 1u << 1;

    static void ServiceThread(void * arg);
    static void PahoMessageHandler(MessageData * md);
    static CHIP_ERROR MapPahoStatus(int status);
    static enum QoS ToPahoQos(MqttQos qos);

    CHIP_ERROR QueueOperation(Operation operation, MqttOperationCallback callback, void * context);
    void ProcessOperation();
    void CompleteOperation(CHIP_ERROR error);

    CHIP_ERROR ProcessInit();
    CHIP_ERROR ProcessDeinit();
    CHIP_ERROR ProcessConnect();
    CHIP_ERROR ProcessDisconnect();
    CHIP_ERROR ProcessSubscribe();
    CHIP_ERROR ProcessUnsubscribe();
    CHIP_ERROR ProcessPublish();
    CHIP_ERROR ProcessYield();

    void FreeTlsContext();
    void LogNetworkConnectError(int status);

    MqttClientConfig mConfig{};
    MqttBroker mPendingBroker{};

    Client mClient{};
    Network mNetwork{};
    uint8_t mTxBuffer[kTxBufferSize] = { 0 };
    uint8_t mRxBuffer[kRxBufferSize] = { 0 };
    sl_ip_address_t mServerIp{};

    const char * mPendingTopic = nullptr;
    ByteSpan mPendingPayload;
    MqttQos mPendingQos = MqttQos::AtLeastOnce;
    bool mPendingRetained = false;
    uint32_t mPendingYieldTimeoutMs = 0;

    void * mThreadId   = nullptr; // osThreadId_t
    void * mEventFlags = nullptr; // osEventFlagsId_t

    volatile bool mBusy         = false;
    bool mInitialized           = false;
    bool mConnected             = false;
    Operation mPendingOperation = Operation::None;

    MqttOperationCallback mUserCallback = nullptr;
    void * mUserCallbackContext         = nullptr;

    MqttMessageCallback mMessageCallback = nullptr;
    void * mMessageCallbackContext       = nullptr;

    static MqttClient * sActiveClient;
};

} // namespace Silabs
} // namespace DeviceLayer
} // namespace chip
