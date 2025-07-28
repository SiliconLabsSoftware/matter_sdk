/**
 * @file
 * @brief Matter abstraction layer for Direct Internet Connectivity.
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

#include "rmc.h"
#include "rmc_config.h"
#include "rmc_nvm_cert.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "MQTT_transport.h"
#include "mqtt.h"

#ifdef __cplusplus
}
#endif

static TaskHandle_t rmcTask         = NULL;
static EventGroupHandle_t rmcEvents = NULL;

mqtt_client_t * mqtt_client        = nullptr;
MQTT_Transport_t * transport       = nullptr;
rmc_subscribe_cb gSubsCB           = NULL;
static mqtt_transport_intf_t trans;

static bool end_loop;
static bool init_complete;

static void rmc_mqtt_subscribe_cb(void * arg, mqtt_err_t err)
{
    const struct mqtt_connect_client_info_t * client_info = (const struct mqtt_connect_client_info_t *) arg;
    (void) client_info;
    ChipLogError(AppServer, "#############[RMC] MQTT sub request callback: %d", (int) err);
}

rmc_err_t rmc_mqtt_subscribe(mqtt_client_t * client, mqtt_incoming_publish_cb_t publish_cb, mqtt_incoming_data_cb_t data_cb,
                             const char * topic, uint8_t qos)
{
    mqtt_set_inpub_callback(mqtt_client, publish_cb, data_cb, NULL);
    int mret = mqtt_subscribe(mqtt_client, topic, qos, rmc_mqtt_subscribe_cb, NULL);
    if (mret != ERR_OK)
    {
        ChipLogError(AppServer, "#############[RMC] MQTT subscribe failed: %d", mret);
        return RMC_ERR_FAIL;
    }
    return RMC_OK;
}

static void rmc_mqtt_conn_cb(mqtt_client_t * client, void * arg, mqtt_connection_status_t status)
{
    (void) client;
    (void) arg;

    ChipLogError(AppServer, "#############[RMC] MQTT connection status: %u", status);
    if (status != MQTT_CONNECT_ACCEPTED)
    {
        if (rmcTask)
            end_loop = true;
        return;
    }
    if (gSubsCB != NULL)
        gSubsCB(); // Subscribe callback
}

void rmc_tcp_connect_cb(err_t err)
{
    ChipLogError(AppServer, "#############[RMC] connection callback started");
    if (err == ERR_OK)
    {
        struct mqtt_connect_client_info_t connect_info;
        mqtt_err_t mret;
        /* Connect to MQTT broker/cloud */
        memset(&connect_info, 0, sizeof(connect_info));

        char clientID[RMC_CLIENTID_LENGTH] = { 0 };
        size_t length                      = 0;

        VerifyOrExit(RMCGetClientId(clientID, RMC_CLIENTID_LENGTH, &length) == CHIP_NO_ERROR,
                     ChipLogError(AppServer, "#############[RMC] failed to fetch client ID"));

        connect_info.client_id = clientID;
#if RMC_CLIENT_USER
        connect_info.client_user = RMC_CLIENT_USER;
#endif // RMC_CLIENT_USER
#if RMC_CLIENT_PASS
        connect_info.client_pass = RMC_CLIENT_PASS;
#endif // RMC_CLIENT_PASS
        connect_info.keep_alive = RMC_KEEP_ALIVE;

        mret = mqtt_client_connect(mqtt_client, (void *) &trans, rmc_mqtt_conn_cb, NULL, &connect_info);
        if (mret != ERR_OK)
        {
            ChipLogError(AppServer, "#############[RMC] MQTT connection failed: %d", mret);
            init_complete = false;
            goto exit;
        }
        init_complete = true;
        return;
    }
    init_complete = false;
exit:
    vTaskDelete(rmcTask);
    vEventGroupDelete(rmcEvents);
    rmcTask = NULL;
    return;
}

static void rmc_task_fn(void * args)
{
    /* get MQTT client handle */
    err_t ret;
    gSubsCB                              = reinterpret_cast<void (*)()>(args);
    mqtt_client                          = mqtt_client_new();
    char ca_cert_buf[RMC_CA_CERT_LENGTH] = { 0 };
    char cert_buf[RMC_DEV_CERT_LENGTH]   = { 0 };
    char key_buf[RMC_DEV_KEY_LENGTH]     = { 0 };
    char hostname[RMC_HOSTNAME_LENGTH]   = { 0 };
    size_t ca_cert_length                = 0;
    size_t cert_length                   = 0;
    size_t key_length                    = 0;
    size_t hostname_length               = 0;

    VerifyOrExit(mqtt_client != NULL, ChipLogError(AppServer, "#############[RMC] failed to create mqtt client"));

    /* Get transport handle*/
    memset(&trans, 0x00, sizeof(mqtt_transport_intf_t));
    transport = MQTT_Transport_Init(&trans, mqtt_client, rmcEvents);
    VerifyOrExit(transport != NULL, ChipLogError(AppServer, "#############[RMC] failed to configure mqtt client"));

    VerifyOrExit(RMCGetHostname(hostname, RMC_HOSTNAME_LENGTH, &hostname_length) == CHIP_NO_ERROR,
                 ChipLogError(AppServer, "#############[RMC] failed to fetch hostname"));
    VerifyOrExit(RMCGetCACertificate(ca_cert_buf, RMC_CA_CERT_LENGTH, &ca_cert_length) == CHIP_NO_ERROR,
                 ChipLogError(AppServer, "#############[RMC] failed to fetch CA certificate"));
    VerifyOrExit(RMCGetDeviceCertificate(cert_buf, RMC_DEV_CERT_LENGTH, &cert_length) == CHIP_NO_ERROR,
                 ChipLogError(AppServer, "#############[RMC] failed to fetch device certificate"));
    VerifyOrExit(RMCGetDevicePrivKey(key_buf, RMC_DEV_KEY_LENGTH, &key_length) == CHIP_NO_ERROR,
                 ChipLogError(AppServer, "#############[RMC] failed to fetch device key"));

    /* set SSL configuration for TLS transport connection */
    if (ca_cert_length > 1 && cert_length > 1 && key_length > 1)
    {
        ret = MQTT_Transport_SSLConfigure(transport, (const u8_t *) ca_cert_buf, ca_cert_length, (const u8_t *) key_buf, key_length,
                                          NULL, 0, (const u8_t *) cert_buf, cert_length);

        VerifyOrExit(ERR_OK == ret, ChipLogError(AppServer, "#############[RMC] failed to configure SSL to mqtt transport"));
    }

    ret = MQTT_Transport_Connect(transport, hostname, RMC_SERVER_PORT, rmc_tcp_connect_cb);
    VerifyOrExit(ERR_OK == ret, ChipLogError(AppServer, "#############[RMC] transport connection failed: %d", ret));

    while (!end_loop)
    {
        EventBits_t event;
        event = xEventGroupWaitBits(
            rmcEvents, SIGNAL_TRANSINTF_RX | SIGNAL_TRANSINTF_TX_ACK | SIGNAL_TRANSINTF_CONN_CLOSE | SIGNAL_TRANSINTF_MBEDTLS_RX, 1,
            0, portMAX_DELAY);
        if (event & SIGNAL_TRANSINTF_CONN_CLOSE)
        {
            mqtt_close(mqtt_client, MQTT_CONNECT_DISCONNECTED);
            end_loop = true;
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
    vTaskDelete(rmcTask);
    rmcTask = NULL;
    vEventGroupDelete(rmcEvents);
    return;
}

void rmc_pub_resp_cb(void * arg, mqtt_err_t err)
{
    (void) arg;
    ChipLogError(AppServer, "#############[RMC] publish data %s", err != MQTT_ERR_OK ? "failed!" : "successful!");
}

rmc_err_t rmc_init(rmc_subscribe_cb subs_cb)
{
    VerifyOrReturnError(rmcTask == NULL, RMC_OK);

    /* Create events group used to receive events from transport layer*/
    rmcEvents = xEventGroupCreate();
    if (!rmcEvents)
    {
        ChipLogError(AppServer, "#############[RMC] failed to create event groups");
        return RMC_ERR_FAIL;
    }

    if ((pdPASS != xTaskCreate(rmc_task_fn, RMC_TASK_NAME, RMC_TASK_STACK_SIZE, (void *) subs_cb, RMC_TASK_PRIORITY, &rmcTask)) ||
        !rmcTask)
    {
        ChipLogError(AppServer, "#############[RMC] failed to create task");
        vEventGroupDelete(rmcEvents);
        return RMC_ERR_MEM;
    }

    /* TODO: Register data and pub callback and subscribe if cloud to app messages are expected*/
    return RMC_OK;
}

rmc_err_t rmc_sendmsg(const char * subject, const char * content)
{
    ChipLogError(AppServer, "##########################sending rmc messgae ");
    if (subject == nullptr || content == nullptr)
    {
        ChipLogError(AppServer, "#############[RMC] invalid subject/content");
        return RMC_ERR_INVAL;
    }
    if (!init_complete)
    {
        ChipLogError(AppServer, "#############[RMC] invalid state");
        return RMC_ERR_CONN;
    }
    rmc_buff_t buffValue;
    buffValue.dataP   = (uint8_t *) content;
    buffValue.dataLen = strlen(content);
    if (MQTT_ERR_OK != mqtt_publish(mqtt_client, subject, buffValue.dataP, buffValue.dataLen, MQTT_QOS_0, 0, rmc_pub_resp_cb, NULL))
    {
        ChipLogError(AppServer, "#############[RMC] failed to publish");
        return RMC_ERR_PUBLISH;
    }
    ChipLogError(AppServer, "##########################sent rmc messgae ");
    return RMC_OK;
}

#ifdef ENABLE_AWS_OTA_FEAT

struct sub_cb_info sub_info;

int rmc_init_status()
{
    if (init_complete)
    {
        return 1;
    }
    else
        return 0;
}

static void rmc_aws_ota_mqtt_incoming_data_cb(void * arg, const char * topic, u16_t topic_len, const u8_t * data, u16_t len,
                                              u8_t flags)
{
    const struct mqtt_connect_client_info_t * client_info = (const struct mqtt_connect_client_info_t *) arg;
    (void) client_info;
    u8_t buff[1500] = { 0 };
    memcpy(buff, data, MIN(len, sizeof(buff)));
    ChipLogError(AppServer, "#############[RMC] incoming_data_callback: len(%u), flags(%u), topic_len(%d)", len, flags, topic_len);
    sub_info.cb(topic, topic_len, (char *) buff, len);
}
static void rmc_aws_ota_mqtt_incoming_publish_cb(void * arg, const char * topic, u32_t tot_len)
{
    const struct mqtt_connect_client_info_t * client_info = (const struct mqtt_connect_client_info_t *) arg;
    (void) client_info;

    ChipLogError(AppServer, "#############[RMC] incoming_publish_cb: topic (%s), len (%d)", topic, (int) tot_len);
}

rmc_err_t rmc_aws_ota_publish(const char * const topic, const char * message, uint32_t message_len, uint8_t qos)
{
    if (!init_complete)
    {
        ChipLogError(AppServer, "#############[RMC] invalid state");
        return RMC_ERR_FAIL;
    }
    if (MQTT_ERR_OK != mqtt_publish(mqtt_client, topic, message, message_len, qos, 0, rmc_pub_resp_cb, NULL))
    {
        ChipLogError(AppServer, "#############[RMC] failed to publish");
        return RMC_ERR_FAIL;
    }
    else
    {
        ChipLogError(AppServer, "#############[RMC] mqtt_publish->message: %s, topic: %s", message, topic);
    }
    return RMC_OK;
}

rmc_err_t rmc_aws_ota_unsubscribe(const char * topic)
{
    if (!init_complete)
    {
        ChipLogError(AppServer, "#############[RMC] invalid state");
        return RMC_ERR_FAIL;
    }

    if (MQTT_ERR_OK != mqtt_unsubscribe(mqtt_client, topic, rmc_mqtt_subscribe_cb, NULL))
    {
        ChipLogError(AppServer, "#############[RMC] failed to unsubscribe");
        return RMC_ERR_FAIL;
    }
    return RMC_OK;
}

rmc_err_t rmc_aws_ota_subscribe(const char * topic, uint8_t qos, callback_t subscribe_cb)
{
    if (!init_complete)
    {
        ChipLogError(AppServer, "#############[RMC] invalid state");
        return RMC_ERR_FAIL;
    }

    sub_info.sub_topic = (char *) topic;
    sub_info.cb        = subscribe_cb;
    if (RMC_OK !=
        rmc_mqtt_subscribe(mqtt_client, rmc_aws_ota_mqtt_incoming_publish_cb, rmc_aws_ota_mqtt_incoming_data_cb, topic, qos))
    {
        ChipLogError(AppServer, "#############[RMC] failed to subscribe");
        return RMC_ERR_FAIL;
    }
    return RMC_OK;
}

rmc_err_t rmc_aws_ota_process()
{
    if (!init_complete)
    {
        ChipLogError(AppServer, "#############[RMC] invalid state");
        return RMC_ERR_FAIL;
    }
    mqtt_process(mqtt_client, 0);

    return RMC_OK;
}

rmc_err_t rmc_aws_ota_close()
{
    if (!init_complete)
    {
        ChipLogError(AppServer, "#############[RMC] invalid state");
        return RMC_ERR_FAIL;
    }

    mqtt_close(mqtt_client, MQTT_CONNECT_DISCONNECTED);

    return RMC_OK;
}
#endif // ENABLE_AWS_OTA_FEAT