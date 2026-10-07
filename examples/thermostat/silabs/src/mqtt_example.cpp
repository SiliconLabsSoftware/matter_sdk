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
#include "jwtcert.h"
#include "cmsis_os2.h"

#include <crypto/CHIPCryptoPAL.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/Span.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>

#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>

#include <cstring>

#if !defined(MBEDTLS_RSA_C) || !defined(MBEDTLS_PKCS1_V15) || !defined(MBEDTLS_PK_PARSE_C)
#error "RS256 JWT requires MBEDTLS_RSA_C, MBEDTLS_PKCS1_V15, and MBEDTLS_PK_PARSE_C"
#endif

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
constexpr char kMqttTopic[]          = MQTT_TOPIC;
constexpr char kMqttPublishMessage[] = MQTT_PUBLISH_MESSAGE;

// header b64 + '.' + claims b64 + '.' + 2048-bit signature b64, plus NUL.
constexpr size_t kMqttJwtCapacity       = 1024;
constexpr size_t kJwtSigningInputMax    = 512;
constexpr size_t kJwtSignatureMax       = 512;
constexpr char kJwtHeaderJson[]         = "{\"alg\":\"RS256\",\"typ\":\"JWT\"}";

char sMqttJwtPassword[kMqttJwtCapacity];
char sJwtSigningInput[kJwtSigningInputMax];
unsigned char sJwtSignature[kJwtSignatureMax];

MqttClient gMqttsClient;
volatile bool gOpDone = false;
CHIP_ERROR gOpResult  = CHIP_NO_ERROR;

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

int JwtRng(void * /* ctx */, unsigned char * out, size_t len)
{
    alignas(uint32_t) unsigned char block[32];

    while (len > 0)
    {
        const size_t chunk   = (len < sizeof(block)) ? len : sizeof(block);
        const size_t aligned = (chunk + 3u) & ~size_t{ 3 };
        if (chip::Crypto::DRBG_get_bytes(block, aligned) != CHIP_NO_ERROR)
        {
            return -1;
        }
        memcpy(out, block, chunk);
        out += chunk;
        len -= chunk;
    }
    return 0;
}

bool Base64UrlAppend(char * out, size_t cap, size_t * used, const uint8_t * in, size_t inLen)
{
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

    for (size_t i = 0; i < inLen; i += 3)
    {
        const uint32_t b0 = in[i];
        const uint32_t b1 = (i + 1 < inLen) ? in[i + 1] : 0;
        const uint32_t b2 = (i + 2 < inLen) ? in[i + 2] : 0;
        const uint32_t n  = (b0 << 16) | (b1 << 8) | b2;
        const size_t produced = (i + 2 < inLen) ? 4 : ((i + 1 < inLen) ? 3 : 2);

        if (*used + produced >= cap)
        {
            return false;
        }

        out[(*used)++] = kAlphabet[(n >> 18) & 63];
        out[(*used)++] = kAlphabet[(n >> 12) & 63];
        if (produced > 2)
        {
            out[(*used)++] = kAlphabet[(n >> 6) & 63];
        }
        if (produced > 3)
        {
            out[(*used)++] = kAlphabet[n & 63];
        }
        out[*used] = '\0';
    }
    return true;
}

void LogMbedtls(const char * what, int ret)
{
    ChipLogError(DeviceLayer, "MQTT JWT %s failed: -0x%04x", what, static_cast<unsigned>(-ret));
}

// RS256: base64url(header).base64url(payload), then PKCS#1 v1.5 over SHA-256 of those bytes.
CHIP_ERROR BuildMqttJwtPassword()
{
    const char * const claims    = MQTT_JWT_CLAIMS;
    const size_t claimsLen       = strlen(claims);
    const size_t headerLen       = sizeof(kJwtHeaderJson) - 1;

    if (claimsLen == 0)
    {
        ChipLogError(DeviceLayer, "MQTT JWT claims are empty");
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    size_t signingLen = 0;
    sJwtSigningInput[0] = '\0';
    if (!Base64UrlAppend(sJwtSigningInput, sizeof(sJwtSigningInput), &signingLen,
                         reinterpret_cast<const uint8_t *>(kJwtHeaderJson), headerLen))
    {
        return CHIP_ERROR_BUFFER_TOO_SMALL;
    }
    if (signingLen + 1 >= sizeof(sJwtSigningInput))
    {
        return CHIP_ERROR_BUFFER_TOO_SMALL;
    }
    sJwtSigningInput[signingLen++] = '.';
    sJwtSigningInput[signingLen]   = '\0';
    if (!Base64UrlAppend(sJwtSigningInput, sizeof(sJwtSigningInput), &signingLen,
                         reinterpret_cast<const uint8_t *>(claims), claimsLen))
    {
        return CHIP_ERROR_BUFFER_TOO_SMALL;
    }

    ChipLogProgress(DeviceLayer, "MQTT JWT signing input: %s", sJwtSigningInput);

    uint8_t hash[chip::Crypto::kSHA256_Hash_Length];
    ReturnErrorOnFailure(chip::Crypto::Hash_SHA256(reinterpret_cast<const uint8_t *>(sJwtSigningInput), signingLen, hash));

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    // PEM length includes the terminating NUL.
    int ret = mbedtls_pk_parse_key(&pk, kJwtCertExample, sizeof(kJwtCertExample), nullptr, 0, JwtRng, nullptr);
    if (ret != 0)
    {
        LogMbedtls("parse key", ret);
        mbedtls_pk_free(&pk);
        return CHIP_ERROR_INTERNAL;
    }

    if (!mbedtls_pk_can_do(&pk, MBEDTLS_PK_RSA))
    {
        ChipLogError(DeviceLayer, "MQTT JWT key is not RSA");
        mbedtls_pk_free(&pk);
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    mbedtls_rsa_context * const rsa = mbedtls_pk_rsa(pk);
    ret = mbedtls_rsa_set_padding(rsa, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_SHA256);
    if (ret != 0)
    {
        LogMbedtls("set padding", ret);
        mbedtls_pk_free(&pk);
        return CHIP_ERROR_INTERNAL;
    }

    const size_t sigLen = mbedtls_rsa_get_len(rsa);
    if (sigLen == 0 || sigLen > sizeof(sJwtSignature))
    {
        mbedtls_pk_free(&pk);
        return CHIP_ERROR_BUFFER_TOO_SMALL;
    }

    // hash is SHA-256(signing input), not the signing input itself. No mode argument in mbed TLS 3.6.
    ret = mbedtls_rsa_pkcs1_sign(rsa, JwtRng, nullptr, MBEDTLS_MD_SHA256, static_cast<unsigned int>(sizeof(hash)), hash,
                                 sJwtSignature);
    mbedtls_pk_free(&pk);
    if (ret != 0)
    {
        LogMbedtls("sign", ret);
        return CHIP_ERROR_INTERNAL;
    }

    if (signingLen + 1 >= sizeof(sMqttJwtPassword))
    {
        return CHIP_ERROR_BUFFER_TOO_SMALL;
    }
    memcpy(sMqttJwtPassword, sJwtSigningInput, signingLen);
    size_t jwtLen = signingLen;
    sMqttJwtPassword[jwtLen++] = '.';
    sMqttJwtPassword[jwtLen]   = '\0';
    if (!Base64UrlAppend(sMqttJwtPassword, sizeof(sMqttJwtPassword), &jwtLen, sJwtSignature, sigLen))
    {
        return CHIP_ERROR_BUFFER_TOO_SMALL;
    }

    ChipLogProgress(DeviceLayer, "MQTT JWT password ready (%u bytes)", static_cast<unsigned>(jwtLen));
    return CHIP_NO_ERROR;
}

// Must not run on the CHIP event-loop thread: osDelay would starve the platform queue
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

CHIP_ERROR SubscribePublish()
{
    CHIP_ERROR err = CHIP_NO_ERROR;

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

sl_status_t mqtt_client_demo_start(void)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    if (!gMqttsClient.IsInitialized())
    {
        err = BuildMqttJwtPassword();
        if (err != CHIP_NO_ERROR)
        {
            ChipLogError(DeviceLayer, "MQTT JWT build failed: %" CHIP_ERROR_FORMAT, err.Format());
            return SL_STATUS_FAIL;
        }

        err = PlatformMgr().AddEventHandler(OnPlatformEvent, 0);
        if (err != CHIP_NO_ERROR)
        {
            ChipLogError(DeviceLayer, "MQTT OnPlatformEvent register failed: %" CHIP_ERROR_FORMAT, err.Format());
            return SL_STATUS_FAIL;
        }

        // Init is queued to the service thread, so Start must happen first when needed.
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

        // Subscribe/Publish without an explicit QoS use MqttClientConfig::subQoS / pubQoS.
        const MqttClientConfig config = {
            .useTls               = true,
            .clientId             = kMqttClientId,
            .username             = kMqttUsername,
            .password             = sMqttJwtPassword,
            .keepAliveIntervalSec = 100,
            .commandTimeoutMs     = 20000,
            .mqttVersion          = 4,
            .cleanSession         = true,
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
        ChipLogProgress(DeviceLayer, "MQTT demo already initialized");
    }

    ChipLogProgress(DeviceLayer, "MQTT ready (use demo mqtt for subscribe/publish)");
    // Prior attempt may have connected then failed on subscribe/publish; skip reconnect.
    if (!gMqttsClient.IsConnected())
    {
        const MqttBroker broker = {
            .brokerIp    = kMqttBrokerIp,
            .tlsHostname = kMqttTlsHostname,
            .brokerPort  = kMqttBrokerPort,
            .clientPort  = kMqttClientPort,
        };
        gOpDone = false;
        err     = RunOperation(gMqttsClient.Connect(broker, OnOperationDone));
        if (err != CHIP_NO_ERROR)
        {
            ChipLogError(DeviceLayer, "MQTT Connect failed: %" CHIP_ERROR_FORMAT, err.Format());
            return SL_STATUS_FAIL;
        }
    }
    else
    {
        ChipLogProgress(DeviceLayer, "MQTT already connected.");
    }
    return SL_STATUS_OK;
}

sl_status_t mqtt_client_demo_run(void)
{
    if (!gMqttsClient.IsRunning() || !gMqttsClient.IsInitialized())
    {
        ChipLogError(DeviceLayer, "MQTT not ready; wait for connectivity start first");
        return SL_STATUS_NOT_INITIALIZED;
    }

    const CHIP_ERROR err = SubscribePublish();
    if (err != CHIP_NO_ERROR)
    {
        return SL_STATUS_FAIL;
    }

    ChipLogProgress(DeviceLayer, "MQTT demo completed (auto-yield keeps session alive)");
    return SL_STATUS_OK;
}

sl_status_t mqtt_client_demo_stop(void)
{
    // Keep Start/Init state so a later connectivity event only needs Connect+Subscribe+Publish.
    if (!gMqttsClient.IsRunning() || !gMqttsClient.IsConnected())
    {
        return SL_STATUS_OK;
    }

    gOpDone              = false;
    const CHIP_ERROR err = RunOperation(gMqttsClient.Disconnect(OnOperationDone));
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "MQTT Disconnect failed: %" CHIP_ERROR_FORMAT, err.Format());
        return SL_STATUS_FAIL;
    }

    ChipLogProgress(DeviceLayer, "MQTT demo disconnected");
    return SL_STATUS_OK;
}

void MqttRunAppEvent(AppEvent * /* aEvent */)
{
    VerifyOrReturn(SL_STATUS_OK == mqtt_client_demo_run(), ChipLogError(DeviceLayer, "mqtt_client_demo_run failed"));
}
