/**
 * @file
 * @brief Matter abstraction layer for AWS.
 *******************************************************************************
 * # License
 * <b>Copyright 2020 Silicon Laboratories Inc.
 *www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon
 *Laboratories Inc. Your use of this software is
 *governed by the terms of Silicon Labs Master
 *Software License Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement.
 *This software is distributed to you in Source Code
 *format and is governed by the sections of the MSLA
 *applicable to Source Code.
 *
 ******************************************************************************/

#include <stdbool.h>
#include <string.h>

#include "FreeRTOS.h"
#include "event_groups.h"
#include "semphr.h"
#include "task.h"

#include "silabs_utils.h"
#include <lib/core/CHIPError.h>
#include <lib/support/CHIPMemString.h>
#include <lib/support/logging/CHIPLogging.h>

#include "MatterAws.h"
#include "MatterAwsConfig.h"
#include "MatterAwsNvmCert.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <lwip/altcp_tls.h>
#include <lwip/dns.h>
#include <lwip/opt.h>
#include <lwip/tcpip.h>

#include <mbedtls/debug.h>

#ifdef __cplusplus
}
#endif

static TaskHandle_t matterAwsTask         = nullptr;
static EventGroupHandle_t matterAwsEvents = nullptr;

mqtt_client_t * mqtt_client             = nullptr;
matterAws_subscribe_cb gSubsCB          = NULL;
static struct altcp_tls_config * tlsCfg = nullptr;

static bool init_complete;

#define SIGNAL_TRANSINTF_CONN_CLOSE 0x04

struct MatterAwsDnsCtx
{
    SemaphoreHandle_t sem;
    ip_addr_t * outAddr;
};

static void MatterAwsDnsCallback(const char * name, const ip_addr_t * ipaddr, void * arg)
{
    (void) name;
    auto * ctx = static_cast<MatterAwsDnsCtx *>(arg);
    if (ipaddr != nullptr)
    {
        *ctx->outAddr = *ipaddr;
    }
    else
    {
        ip_addr_set_zero(ctx->outAddr);
    }
    xSemaphoreGive(ctx->sem);
}

static err_t MatterAwsResolveHostname(const char * hostname, ip_addr_t * addr)
{
    SemaphoreHandle_t sem = xSemaphoreCreateBinary();
    VerifyOrReturnValue(sem != nullptr, ERR_MEM);

    MatterAwsDnsCtx ctx = { sem, addr };

    LOCK_TCPIP_CORE();
    err_t derr = dns_gethostbyname(hostname, addr, MatterAwsDnsCallback, &ctx);
    UNLOCK_TCPIP_CORE();

    if (derr == ERR_INPROGRESS)
    {
        xSemaphoreTake(sem, portMAX_DELAY);
        derr = ip_addr_isany(addr) ? ERR_VAL : ERR_OK;
    }

    vSemaphoreDelete(sem);
    return derr;
}

static void MatterAwsMqttSubscribeCb(void * arg, err_t err)
{
    const struct mqtt_connect_client_info_t * client_info = (const struct mqtt_connect_client_info_t *) arg;
    (void) client_info;
    ChipLogProgress(AppServer, "[MATTER_AWS] MQTT sub request callback: %d", (int) err);
}

matterAws_err_t MatterAwsMqttSubscribe(mqtt_client_t * client, mqtt_incoming_publish_cb_t publish_cb,
                                       mqtt_incoming_data_cb_t data_cb, const char * topic, uint8_t qos)
{
    (void) client;
    VerifyOrReturnValue(mqtt_client != nullptr, MATTER_AWS_ERR_FAIL);

    LOCK_TCPIP_CORE();
    mqtt_set_inpub_callback(mqtt_client, publish_cb, data_cb, NULL);
    err_t mret = mqtt_subscribe(mqtt_client, topic, qos, MatterAwsMqttSubscribeCb, NULL);
    UNLOCK_TCPIP_CORE();

    if (mret != ERR_OK)
    {
        ChipLogError(AppServer, "[MATTER_AWS] MQTT subscribe failed: %d", mret);
        return MATTER_AWS_ERR_FAIL;
    }
    return MATTER_AWS_OK;
}

static void MatterAwsMqttConnCb(mqtt_client_t * client, void * arg, mqtt_connection_status_t status)
{
    (void) client;
    (void) arg;

    ChipLogProgress(AppServer, "[MATTER_AWS] MQTT connection status: %u", status);
    if (status != MQTT_CONNECT_ACCEPTED)
    {
        init_complete = false;
        VerifyOrReturn(matterAwsTask != nullptr && matterAwsEvents != nullptr,
                       ChipLogError(AppServer, "[MATTER_AWS] Task or events not initialized in MQTT callback"));
        xEventGroupSetBits(matterAwsEvents, SIGNAL_TRANSINTF_CONN_CLOSE);
        return;
    }
    init_complete = true;
    if (gSubsCB != NULL)
    {
        gSubsCB();
    }
}

static void MatterAwsCleanupClient()
{
    if (mqtt_client != nullptr)
    {
        LOCK_TCPIP_CORE();
        mqtt_disconnect(mqtt_client);
        mqtt_client_free(mqtt_client);
        UNLOCK_TCPIP_CORE();
        mqtt_client = nullptr;
    }
    if (tlsCfg != nullptr)
    {
        LOCK_TCPIP_CORE();
        altcp_tls_free_config(tlsCfg);
        UNLOCK_TCPIP_CORE();
        tlsCfg = nullptr;
    }
}

static void MatterAwsTaskFn(void * args)
{
    bool endLoop = false;
#if ALTCP_MBEDTLS_LIB_DEBUG
    mbedtls_debug_set_threshold(ALTCP_MBEDTLS_LIB_DEBUG_LEVEL_MIN);
#endif

    gSubsCB = reinterpret_cast<void (*)()>(args);

    char ca_cert_buf[MATTER_AWS_CA_CERT_LENGTH] = { 0 };
    char cert_buf[MATTER_AWS_DEV_CERT_LENGTH]   = { 0 };
    char key_buf[MATTER_AWS_DEV_KEY_LENGTH]     = { 0 };
    char hostname[MATTER_AWS_HOSTNAME_LENGTH]   = { 0 };
    static char clientIdBuf[MATTER_AWS_CLIENTID_LENGTH];
    size_t ca_cert_length  = 0;
    size_t cert_length     = 0;
    size_t key_length      = 0;
    size_t hostname_length = 0;
    size_t clientIdLen     = 0;
    ip_addr_t brokerAddr;

    VerifyOrExit(MatterAwsGetHostname(hostname, sizeof(hostname), &hostname_length) == CHIP_NO_ERROR,
                 ChipLogError(AppServer, "[MATTER_AWS] failed to fetch hostname"));
    hostname[sizeof(hostname) - 1] = '\0';

    VerifyOrExit(MatterAwsGetCACertificate(ca_cert_buf, sizeof(ca_cert_buf), &ca_cert_length) == CHIP_NO_ERROR,
                 ChipLogError(AppServer, "[MATTER_AWS] failed to fetch CA certificate"));
    VerifyOrExit(MatterAwsGetDeviceCertificate(cert_buf, sizeof(cert_buf), &cert_length) == CHIP_NO_ERROR,
                 ChipLogError(AppServer, "[MATTER_AWS] failed to fetch device certificate"));
    VerifyOrExit(MatterAwsGetDevicePrivKey(key_buf, sizeof(key_buf), &key_length) == CHIP_NO_ERROR,
                 ChipLogError(AppServer, "[MATTER_AWS] failed to fetch device key"));
    VerifyOrExit(MatterAwsGetClientId(clientIdBuf, sizeof(clientIdBuf), &clientIdLen) == CHIP_NO_ERROR,
                 ChipLogError(AppServer, "[MATTER_AWS] failed to fetch client ID"));
    clientIdBuf[sizeof(clientIdBuf) - 1] = '\0';

    VerifyOrExit(err_t err = MatterAwsResolveHostname(hostname, &brokerAddr) && err == ERR_OK,
                 ChipLogError(AppServer, "[MATTER_AWS] DNS resolution failed: %d", err));

    LOCK_TCPIP_CORE();
    mqtt_client = mqtt_client_new();
    UNLOCK_TCPIP_CORE();
    VerifyOrExit(mqtt_client != nullptr, ChipLogError(AppServer, "[MATTER_AWS] failed to create mqtt client"));

#if LWIP_ALTCP && LWIP_ALTCP_TLS
    if (ca_cert_length > 1 && cert_length > 1 && key_length > 1)
    {
        LOCK_TCPIP_CORE();
        tlsCfg = altcp_tls_create_config_client_2wayauth(reinterpret_cast<const u8_t *>(ca_cert_buf), ca_cert_length,
                                                         reinterpret_cast<const u8_t *>(key_buf), key_length, NULL, 0,
                                                         reinterpret_cast<const u8_t *>(cert_buf), cert_length);
        UNLOCK_TCPIP_CORE();
        VerifyOrExit(tlsCfg != nullptr, ChipLogError(AppServer, "[MATTER_AWS] failed to configure TLS"));
    }
#endif

    {
        struct mqtt_connect_client_info_t connect_info;
        memset(&connect_info, 0, sizeof(connect_info));
        connect_info.client_id  = clientIdBuf;
        connect_info.keep_alive = MATTER_AWS_KEEP_ALIVE;
#if MATTER_AWS_CLIENT_USER
        connect_info.client_user = MATTER_AWS_CLIENT_USER;
#endif
#if MATTER_AWS_CLIENT_PASS
        connect_info.client_pass = MATTER_AWS_CLIENT_PASS;
#endif
#if LWIP_ALTCP && LWIP_ALTCP_TLS
        connect_info.tls_config = tlsCfg;
#endif

        LOCK_TCPIP_CORE();
        err_t mret =
            mqtt_client_connect(mqtt_client, &brokerAddr, MATTER_AWS_SERVER_PORT, MatterAwsMqttConnCb, NULL, &connect_info);
        UNLOCK_TCPIP_CORE();

        VerifyOrExit(mret == ERR_OK, ChipLogError(AppServer, "[MATTER_AWS] MQTT connect failed: %d", mret));
    }

    while (!endLoop)
    {
        EventBits_t event = xEventGroupWaitBits(matterAwsEvents, SIGNAL_TRANSINTF_CONN_CLOSE, pdTRUE, pdFALSE, portMAX_DELAY);
        if (event & SIGNAL_TRANSINTF_CONN_CLOSE)
        {
            LOCK_TCPIP_CORE();
            mqtt_disconnect(mqtt_client);
            UNLOCK_TCPIP_CORE();
            endLoop = true;
        }
    }
    init_complete = false;

exit:
    MatterAwsCleanupClient();

    if (matterAwsEvents != nullptr)
    {
        vEventGroupDelete(matterAwsEvents);
        matterAwsEvents = nullptr;
    }
    matterAwsTask = nullptr;
    vTaskDelete(NULL);
}

void MatterAwsPubRespCb(void * arg, err_t err)
{
    (void) arg;
    ChipLogProgress(AppServer, "[MATTER_AWS] publish data %s", err != ERR_OK ? "failed!" : "successful!");
}

matterAws_err_t MatterAwsInit(matterAws_subscribe_cb subs_cb)
{
    VerifyOrReturnError(matterAwsTask == nullptr, MATTER_AWS_OK);

    matterAwsEvents = xEventGroupCreate();
    if (!matterAwsEvents)
    {
        ChipLogError(AppServer, "[MATTER_AWS] failed to create event groups");
        return MATTER_AWS_ERR_FAIL;
    }

    if ((pdPASS !=
         xTaskCreate(MatterAwsTaskFn, MATTER_AWS_TASK_NAME, MATTER_AWS_TASK_STACK_SIZE, (void *) subs_cb, MATTER_AWS_TASK_PRIORITY,
                     &matterAwsTask)) ||
        !matterAwsTask)
    {
        ChipLogError(AppServer, "[MATTER_AWS] failed to create task");
        vEventGroupDelete(matterAwsEvents);
        matterAwsEvents = nullptr;
        return MATTER_AWS_ERR_MEM;
    }

    return MATTER_AWS_OK;
}

matterAws_err_t MatterAwsSendMsg(const char * subject, const char * content)
{
    if (subject == nullptr || content == nullptr)
    {
        ChipLogError(AppServer, "[MATTER_AWS] invalid subject/content");
        return MATTER_AWS_ERR_INVAL;
    }
    if (!init_complete)
    {
        ChipLogError(AppServer, "[MATTER_AWS] invalid state");
        return MATTER_AWS_ERR_CONN;
    }
    size_t len = strlen(content);
    if (len > UINT16_MAX)
    {
        ChipLogError(AppServer, "[MATTER_AWS] payload too large");
        return MATTER_AWS_ERR_INVAL;
    }
    auto payloadLen = static_cast<u16_t>(len);

    LOCK_TCPIP_CORE();
    err_t pr = mqtt_publish(mqtt_client, subject, content, payloadLen, MQTT_QOS_0, 0, MatterAwsPubRespCb, NULL);
    UNLOCK_TCPIP_CORE();

    if (pr != ERR_OK)
    {
        ChipLogError(AppServer, "[MATTER_AWS] failed to publish");
        return MATTER_AWS_ERR_PUBLISH;
    }
    return MATTER_AWS_OK;
}

#ifdef SL_MATTER_ENABLE_AWS_OTA_FEAT

struct sub_cb_info sub_info;

static char sMatterAwsOtaIncomingTopic[192];

int MatterAwsInitStatus()
{
    return init_complete ? 1 : 0;
}

static void MatterAwsOtaMqttIncomingDataCb(void * arg, const u8_t * data, u16_t len, u8_t flags)
{
    (void) arg;
    (void) flags;
    u8_t buff[1500] = { 0 };
    memcpy(buff, data, LWIP_MIN(len, sizeof(buff)));
    uint16_t tlen = static_cast<uint16_t>(strnlen(sMatterAwsOtaIncomingTopic, sizeof(sMatterAwsOtaIncomingTopic)));
    ChipLogProgress(AppServer, "[MATTER_AWS] incoming_data_callback: len(%u), flags(%u)", len, flags);
    sub_info.cb(sMatterAwsOtaIncomingTopic, tlen, reinterpret_cast<char *>(buff), len);
}

static void MatterAwsOtaMqttIncomingPublishCb(void * arg, const char * topic, u32_t tot_len)
{
    (void) arg;
    (void) tot_len;
    chip::Platform::CopyString(sMatterAwsOtaIncomingTopic, topic);
    ChipLogProgress(AppServer, "[MATTER_AWS] incoming_publish_cb: topic (%s), len (%d)", topic, static_cast<int>(tot_len));
}

matterAws_err_t MatterAwsOtaPublish(const char * const topic, const char * message, uint32_t message_len, uint8_t qos)
{
    if (!init_complete)
    {
        ChipLogError(AppServer, "[MATTER_AWS] invalid state");
        return MATTER_AWS_ERR_FAIL;
    }
    if (message_len > UINT16_MAX)
    {
        ChipLogError(AppServer, "[MATTER_AWS] OTA publish length overflow");
        return MATTER_AWS_ERR_FAIL;
    }
    auto plen = static_cast<u16_t>(message_len);

    LOCK_TCPIP_CORE();
    err_t pr = mqtt_publish(mqtt_client, topic, message, plen, qos, 0, MatterAwsPubRespCb, NULL);
    UNLOCK_TCPIP_CORE();

    if (pr != ERR_OK)
    {
        ChipLogError(AppServer, "[MATTER_AWS] failed to publish");
        return MATTER_AWS_ERR_FAIL;
    }
    ChipLogError(AppServer, "[MATTER_AWS] mqtt_publish->message: %s, topic: %s", message, topic);
    return MATTER_AWS_OK;
}

matterAws_err_t MatterAwsOtaUnsubscribe(const char * topic)
{
    if (!init_complete)
    {
        ChipLogError(AppServer, "[MATTER_AWS] invalid state");
        return MATTER_AWS_ERR_FAIL;
    }

    LOCK_TCPIP_CORE();
    err_t ur = mqtt_unsubscribe(mqtt_client, topic, MatterAwsMqttSubscribeCb, NULL);
    UNLOCK_TCPIP_CORE();

    if (ur != ERR_OK)
    {
        ChipLogError(AppServer, "[MATTER_AWS] failed to unsubscribe");
        return MATTER_AWS_ERR_FAIL;
    }
    return MATTER_AWS_OK;
}

matterAws_err_t MatterAwsOtaSubscribe(const char * topic, uint8_t qos, callback_t subscribe_cb)
{
    if (!init_complete)
    {
        ChipLogError(AppServer, "[MATTER_AWS] invalid state");
        return MATTER_AWS_ERR_FAIL;
    }

    sub_info.sub_topic = const_cast<char *>(topic);
    sub_info.cb        = subscribe_cb;
    return MatterAwsMqttSubscribe(mqtt_client, MatterAwsOtaMqttIncomingPublishCb, MatterAwsOtaMqttIncomingDataCb, topic, qos);
}

matterAws_err_t MatterAwsOtaProcess()
{
    if (!init_complete)
    {
        ChipLogError(AppServer, "[MATTER_AWS] invalid state");
        return MATTER_AWS_ERR_FAIL;
    }
    return MATTER_AWS_OK;
}

matterAws_err_t MatterAwsOtaClose()
{
    if (!init_complete)
    {
        ChipLogError(AppServer, "[MATTER_AWS] invalid state");
        return MATTER_AWS_ERR_FAIL;
    }

    LOCK_TCPIP_CORE();
    mqtt_disconnect(mqtt_client);
    UNLOCK_TCPIP_CORE();

    return MATTER_AWS_OK;
}
#endif // SL_MATTER_ENABLE_AWS_OTA_FEAT
