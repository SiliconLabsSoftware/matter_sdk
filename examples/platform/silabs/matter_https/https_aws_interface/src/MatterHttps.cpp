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
#include "task.h"

#include "silabs_utils.h"
#include <lib/core/CHIPError.h>
#include <lib/support/logging/CHIPLogging.h>

#include "MatterHttps.h"
#include "MatterHttpsConfig.h"
#include "MatterHttpsNvmCert.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "MQTT_transport.h"
#include "mqtt.h"

#ifdef __cplusplus
}
#endif

static TaskHandle_t matterHttpsTask         = nullptr;
static EventGroupHandle_t matterHttpsEvents = nullptr;

mqtt_client_t * mqtt_client    = nullptr;
MQTT_Transport_t * transport   = nullptr;
matterHttps_subscribe_cb gSubsCB = NULL;
static mqtt_transport_intf_t trans;

static bool init_complete;

static void MatterHttpsMqttSubscribeCb(void * arg, mqtt_err_t err)
{
    const struct mqtt_connect_client_info_t * client_info = (const struct mqtt_connect_client_info_t *) arg;
    (void) client_info;
    ChipLogProgress(AppServer, "[MATTER_HTTPS] MQTT sub request callback: %d", (int) err);
}

matterHttps_err_t MatterHttpsMqttSubscribe(mqtt_client_t * client, mqtt_incoming_publish_cb_t publish_cb,
                                       mqtt_incoming_data_cb_t data_cb, const char * topic, uint8_t qos)
{
    mqtt_set_inpub_callback(mqtt_client, publish_cb, data_cb, NULL);
    int mret = mqtt_subscribe(mqtt_client, topic, qos, MatterHttpsMqttSubscribeCb, NULL);
    if (mret != ERR_OK)
    {
        ChipLogError(AppServer, "[MATTER_HTTPS] MQTT subscribe failed: %d", mret);
        return MATTER_HTTPS_ERR_FAIL;
    }
    return MATTER_HTTPS_OK;
}

static void MatterHttpsMqttConnCb(mqtt_client_t * client, void * arg, mqtt_connection_status_t status)
{
    (void) client;
    (void) arg;

    ChipLogProgress(AppServer, "[MATTER_HTTPS] MQTT connection status: %u", status);
    if (status != MQTT_CONNECT_ACCEPTED)
    {
        /* Signal the task to exit cleanly - the task will handle the event and set its local endLoop flag */
        VerifyOrReturn(matterHttpsTask != nullptr && matterHttpsEvents != nullptr,
                       ChipLogError(AppServer, "[MATTER_HTTPS] Task or events not initialized in MQTT callback"));
        xEventGroupSetBits(matterHttpsEvents, SIGNAL_TRANSINTF_CONN_CLOSE);
        return;
    }
    if (gSubsCB != NULL)
        gSubsCB(); // Subscribe callback
}

void MatterHttpsTcpConnectCb(err_t err)
{
    ChipLogProgress(AppServer, "[MATTER_HTTPS] connection callback started");
    if (err == ERR_OK)
    {
        struct mqtt_connect_client_info_t connect_info;
        mqtt_err_t mret;
        /* Connect to MQTT broker/cloud */
        memset(&connect_info, 0, sizeof(connect_info));

        char clientID[MATTER_HTTPS_CLIENTID_LENGTH] = { 0 };
        size_t length                             = 0;

        VerifyOrExit(MatterHttpsGetClientId(clientID, MATTER_HTTPS_CLIENTID_LENGTH, &length) == CHIP_NO_ERROR,
                     ChipLogError(AppServer, "[MATTER_HTTPS] failed to fetch client ID"));

        connect_info.client_id = clientID;
#if MATTER_HTTPS_CLIENT_USER
        connect_info.client_user = MATTER_HTTPS_CLIENT_USER;
#endif // MATTER_HTTPS_CLIENT_USER
#if MATTER_HTTPS_CLIENT_PASS
        connect_info.client_pass = MATTER_HTTPS_CLIENT_PASS;
#endif // MATTER_HTTPS_CLIENT_PASS
        connect_info.keep_alive = MATTER_HTTPS_KEEP_ALIVE;

        mret = mqtt_client_connect(mqtt_client, (void *) &trans, MatterHttpsMqttConnCb, NULL, &connect_info);
        if (mret != ERR_OK)
        {
            ChipLogError(AppServer, "[MATTER_HTTPS] MQTT connection failed: %d", mret);
            init_complete = false;
            goto exit;
        }
        init_complete = true;
        return;
    }
    init_complete = false;
exit:
    /* Instead of deleting the task from callback context (which causes hardfault),
     * signal the task to exit and let it clean up itself. The callback is called
     * from TCP/IP thread context, and deleting a task from another task's context
     * can corrupt FreeRTOS internal structures. */
    VerifyOrReturn(matterHttpsTask != nullptr && matterHttpsEvents != nullptr,
                   ChipLogError(AppServer, "[MATTER_HTTPS] Task or events not initialized in TCP callback"));
    /* Signal the task to wake up and exit cleanly - task will set its local endLoop flag */
    xEventGroupSetBits(matterHttpsEvents, SIGNAL_TRANSINTF_CONN_CLOSE);
    return;
}

static void MatterHttpsTaskFn(void * args)
{
    /* Local flag to control the task loop - reset each time the task starts */
    bool endLoop = false;

    /* get MQTT client handle */
    err_t ret;
    gSubsCB                                     = reinterpret_cast<void (*)()>(args);
    mqtt_client                                 = mqtt_client_new();
    char ca_cert_buf[MATTER_HTTPS_CA_CERT_LENGTH] = { 0 };
    char cert_buf[MATTER_HTTPS_DEV_CERT_LENGTH]   = { 0 };
    char key_buf[MATTER_HTTPS_DEV_KEY_LENGTH]     = { 0 };
    char hostname[MATTER_HTTPS_HOSTNAME_LENGTH]   = { 0 };
    size_t ca_cert_length                       = 0;
    size_t cert_length                          = 0;
    size_t key_length                           = 0;
    size_t hostname_length                      = 0;

    VerifyOrExit(mqtt_client != NULL, ChipLogError(AppServer, "[MATTER_HTTPS] failed to create mqtt client"));

    /* Get transport handle*/
    memset(&trans, 0x00, sizeof(mqtt_transport_intf_t));
    transport = MQTT_Transport_Init(&trans, mqtt_client, matterHttpsEvents);
    VerifyOrExit(transport != NULL, ChipLogError(AppServer, "[MATTER_HTTPS] failed to configure mqtt client"));

    VerifyOrExit(MatterHttpsGetHostname(hostname, MATTER_HTTPS_HOSTNAME_LENGTH, &hostname_length) == CHIP_NO_ERROR,
                 ChipLogError(AppServer, "[MATTER_HTTPS] failed to fetch hostname"));
    VerifyOrExit(MatterHttpsGetCACertificate(ca_cert_buf, MATTER_HTTPS_CA_CERT_LENGTH, &ca_cert_length) == CHIP_NO_ERROR,
                 ChipLogError(AppServer, "[MATTER_HTTPS] failed to fetch CA certificate"));
    VerifyOrExit(MatterHttpsGetDeviceCertificate(cert_buf, MATTER_HTTPS_DEV_CERT_LENGTH, &cert_length) == CHIP_NO_ERROR,
                 ChipLogError(AppServer, "[MATTER_HTTPS] failed to fetch device certificate"));
    VerifyOrExit(MatterHttpsGetDevicePrivKey(key_buf, MATTER_HTTPS_DEV_KEY_LENGTH, &key_length) == CHIP_NO_ERROR,
                 ChipLogError(AppServer, "[MATTER_HTTPS] failed to fetch device key"));

    /* set SSL configuration for TLS transport connection */
    if (ca_cert_length > 1 && cert_length > 1 && key_length > 1)
    {
        ret = MQTT_Transport_SSLConfigure(transport, (const u8_t *) ca_cert_buf, ca_cert_length, (const u8_t *) key_buf, key_length,
                                          NULL, 0, (const u8_t *) cert_buf, cert_length);

        VerifyOrExit(ERR_OK == ret, ChipLogError(AppServer, "[MATTER_HTTPS] failed to configure SSL to mqtt transport"));
    }

    ret = MQTT_Transport_Connect(transport, hostname, hostname_length, MATTER_HTTPS_SERVER_PORT, MatterHttpsTcpConnectCb);
    VerifyOrExit(ERR_OK == ret, ChipLogError(AppServer, "[MATTER_HTTPS] transport connection failed: %d", ret));

    while (!endLoop)
    {
        EventBits_t event;
        event = xEventGroupWaitBits(matterHttpsEvents,
                                    SIGNAL_TRANSINTF_RX | SIGNAL_TRANSINTF_TX_ACK | SIGNAL_TRANSINTF_CONN_CLOSE |
                                        SIGNAL_TRANSINTF_MBEDTLS_RX,
                                    1, 0, portMAX_DELAY);
        if (event & SIGNAL_TRANSINTF_CONN_CLOSE)
        {
            mqtt_close(mqtt_client, MQTT_CONNECT_DISCONNECTED);
            endLoop = true;
        }
        else
        {
            if (event & SIGNAL_TRANSINTF_RX)
                mqtt_process(mqtt_client, SIGNAL_TRANSINTF_TX);
            else if (event & SIGNAL_TRANSINTF_TX_ACK)
                mqtt_process(mqtt_client, SIGNAL_TRANSINTF_TX_ACK);
            if (event & SIGNAL_TRANSINTF_MBEDTLS_RX)
                transport_process_mbedtls_rx(transport);
        }
    }
    init_complete = false;

exit:
    /* Clean up resources before task deletion.
     * Save handle and nullify pointers atomically to prevent race with callbacks. */
    if (matterHttpsTask != nullptr)
    {
        vEventGroupDelete(matterHttpsEvents);
        matterHttpsEvents = nullptr;
        matterHttpsTask   = nullptr;
    }
    /* Delete the current task - use NULL to delete self */
    vTaskDelete(NULL);
    /* This line should never be reached */
    return;
}

void MatterHttpsPubRespCb(void * arg, mqtt_err_t err)
{
    (void) arg;
    ChipLogProgress(AppServer, "[MATTER_HTTPS] publish data %s", err != MQTT_ERR_OK ? "failed!" : "successful!");
}

matterHttps_err_t MatterHttpsInit(matterHttps_subscribe_cb subs_cb)
{
    VerifyOrReturnError(matterHttpsTask == nullptr, MATTER_HTTPS_OK);

    /* Create events group used to receive events from transport layer*/
    matterHttpsEvents = xEventGroupCreate();
    if (!matterHttpsEvents)
    {
        ChipLogError(AppServer, "[MATTER_HTTPS] failed to create event groups");
        return MATTER_HTTPS_ERR_FAIL;
    }

    if ((pdPASS !=
         xTaskCreate(MatterHttpsTaskFn, MATTER_HTTPS_TASK_NAME, MATTER_HTTPS_TASK_STACK_SIZE, (void *) subs_cb, MATTER_HTTPS_TASK_PRIORITY,
                     &matterHttpsTask)) ||
        !matterHttpsTask)
    {
        ChipLogError(AppServer, "[MATTER_HTTPS] failed to create task");
        vEventGroupDelete(matterHttpsEvents);
        return MATTER_HTTPS_ERR_MEM;
    }

    /* TODO: Register data and pub callback and subscribe if cloud to app messages are expected*/
    return MATTER_HTTPS_OK;
}

matterHttps_err_t MatterHttpsSendMsg(const char * subject, const char * content)
{
    if (subject == nullptr || content == nullptr)
    {
        ChipLogError(AppServer, "[MATTER_HTTPS] invalid subject/content");
        return MATTER_HTTPS_ERR_INVAL;
    }
    if (!init_complete)
    {
        ChipLogError(AppServer, "[MATTER_HTTPS] invalid state");
        return MATTER_HTTPS_ERR_CONN;
    }
    matterHttps_buff_t buffValue;
    buffValue.dataP   = (uint8_t *) content;
    buffValue.dataLen = strlen(content);
    if (MQTT_ERR_OK !=
        mqtt_publish(mqtt_client, subject, buffValue.dataP, buffValue.dataLen, MQTT_QOS_0, 0, MatterHttpsPubRespCb, NULL))
    {
        ChipLogError(AppServer, "[MATTER_HTTPS] failed to publish");
        return MATTER_HTTPS_ERR_PUBLISH;
    }
    return MATTER_HTTPS_OK;
}

#ifdef SL_MATTER_ENABLE_HTTPS_OTA_FEAT

struct sub_cb_info sub_info;

int MatterHttpsInitStatus()
{
    if (init_complete)
    {
        return 1;
    }
    else
        return 0;
}

static void MatterHttpsOtaMqttIncomingDataCb(void * arg, const char * topic, u16_t topic_len, const u8_t * data, u16_t len,
                                           u8_t flags)
{
    const struct mqtt_connect_client_info_t * client_info = (const struct mqtt_connect_client_info_t *) arg;
    (void) client_info;
    u8_t buff[1500] = { 0 };
    memcpy(buff, data, LWIP_MIN(len, sizeof(buff)));
    ChipLogProgress(AppServer, "[MATTER_HTTPS] incoming_data_callback: len(%u), flags(%u), topic_len(%d)", len, flags, topic_len);
    sub_info.cb(topic, topic_len, (char *) buff, len);
}
static void MatterHttpsOtaMqttIncomingPublishCb(void * arg, const char * topic, u32_t tot_len)
{
    const struct mqtt_connect_client_info_t * client_info = (const struct mqtt_connect_client_info_t *) arg;
    (void) client_info;

    ChipLogProgress(AppServer, "[MATTER_HTTPS] incoming_publish_cb: topic (%s), len (%d)", topic, (int) tot_len);
}

matterHttps_err_t MatterHttpsOtaPublish(const char * const topic, const char * message, uint32_t message_len, uint8_t qos)
{
    if (!init_complete)
    {
        ChipLogError(AppServer, "[MATTER_HTTPS] invalid state");
        return MATTER_HTTPS_ERR_FAIL;
    }
    if (MQTT_ERR_OK != mqtt_publish(mqtt_client, topic, message, message_len, qos, 0, MatterHttpsPubRespCb, NULL))
    {
        ChipLogError(AppServer, "[MATTER_HTTPS] failed to publish");
        return MATTER_HTTPS_ERR_FAIL;
    }
    else
    {
        ChipLogError(AppServer, "[MATTER_HTTPS] mqtt_publish->message: %s, topic: %s", message, topic);
    }
    return MATTER_HTTPS_OK;
}

matterHttps_err_t MatterHttpsOtaUnsubscribe(const char * topic)
{
    if (!init_complete)
    {
        ChipLogError(AppServer, "[MATTER_HTTPS] invalid state");
        return MATTER_HTTPS_ERR_FAIL;
    }

    if (MQTT_ERR_OK != mqtt_unsubscribe(mqtt_client, topic, MatterHttpsMqttSubscribeCb, NULL))
    {
        ChipLogError(AppServer, "[MATTER_HTTPS] failed to unsubscribe");
        return MATTER_HTTPS_ERR_FAIL;
    }
    return MATTER_HTTPS_OK;
}

matterHttps_err_t MatterHttpsOtaSubscribe(const char * topic, uint8_t qos, callback_t subscribe_cb)
{
    if (!init_complete)
    {
        ChipLogError(AppServer, "[MATTER_HTTPS] invalid state");
        return MATTER_HTTPS_ERR_FAIL;
    }

    sub_info.sub_topic = (char *) topic;
    sub_info.cb        = subscribe_cb;
    if (MATTER_HTTPS_OK !=
        MatterHttpsMqttSubscribe(mqtt_client, MatterHttpsOtaMqttIncomingPublishCb, MatterHttpsOtaMqttIncomingDataCb, topic, qos))
    {
        ChipLogError(AppServer, "[MATTER_HTTPS] failed to subscribe");
        return MATTER_HTTPS_ERR_FAIL;
    }
    return MATTER_HTTPS_OK;
}

matterHttps_err_t MatterHttpsOtaProcess()
{
    if (!init_complete)
    {
        ChipLogError(AppServer, "[MATTER_HTTPS] invalid state");
        return MATTER_HTTPS_ERR_FAIL;
    }
    mqtt_process(mqtt_client, 0);

    return MATTER_HTTPS_OK;
}

matterHttps_err_t MatterHttpsOtaClose()
{
    if (!init_complete)
    {
        ChipLogError(AppServer, "[MATTER_HTTPS] invalid state");
        return MATTER_HTTPS_ERR_FAIL;
    }

    mqtt_close(mqtt_client, MQTT_CONNECT_DISCONNECTED);

    return MATTER_HTTPS_OK;
}
#endif // SL_MATTER_ENABLE_HTTPS_OTA_FEAT
