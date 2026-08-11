/***************************************************************************/
/**
 * @file mqtt_client.c
 * @brief Paho MQTT/MQTTS client demo for Matter dual-stack (NWP sockets + host LwIP)
 *******************************************************************************
 * # License
 * <b>Copyright 2025 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 ******************************************************************************/

#include "mqtt_client.h"

#include "MQTTClient.h"
#include "cacert.pem.h"
#include "cmsis_os2.h"
#include "silabs_utils.h"
#include "sl_constants.h"
#include "sl_net.h"
#include "sl_status.h"
#include "stdbool.h"
#include <stdlib.h>
#include <string.h>

#ifndef MQTT_USE_HOST_LWIP_TLS
#define MQTT_USE_HOST_LWIP_TLS 0
#endif

#define MQTT_BROKER_IP "192.168.0.191"
#define MQTT_BROKER_TLS_HOSTNAME "mqtt.local"

#define MQTT_BROKER_PORT 1883
#define MQTTS_BROKER_PORT 8883
#define MQTT_CLIENT_PORT 5003

#define MQTT_TLS_CERTIFICATE_INDEX 1

#define MQTT_USERNAME "username"
#define MQTT_PASSWORD "password"

#define MQTT_QOS 1
#define TOPIC_TO_BE_SUBSCRIBED "THERMOSTAT-DATA"

#define TCP_MQTT_CLIENT_INIT_BUFF_LEN 3500
#define TCP_MQTT_CLIENT_RX_BUFFER_SIZE 1500
#define TCP_MQTT_CLIENT_TX_BUFFER_SIZE 1500
#define TCP_MQTT_CONNECT_TIME_OUT 20000
#define KEEP_ALIVE_PERIOD 100
#define MQTT_VERSION 4

#define LAST_WILL_TOPIC "WISECONNECT-SDK-MQTT-CLIENT-LAST-WILL"
#define LAST_WILL_MESSAGE "WISECONNECT-SDK-MQTT-CLIENT has been disconnect from network"
#define QOS_OF_LAST_WILL 1
#define IS_LAST_WILL_RETAINED 0
#define WILL_FLAG_ENABLE 0

static volatile int halt = 0;

static bool enable_ssl           = true;
static int8_t clientID[]         = "WISECONNECT_SDK_TOPIC";
static uint8_t publish_message[] = "THIS IS MQTT CLIENT DEMO FROM APPLICATION";

static int8_t tcp_mqtt_client_buffer[TCP_MQTT_CLIENT_INIT_BUFF_LEN];

static osThreadId_t s_mqtt_demo_thread_id = NULL;

static const osThreadAttr_t mqtt_lwip_thread_attributes = {
    .name       = "mqtt_lwip",
    .attr_bits  = 0,
    .cb_mem     = 0,
    .cb_size    = 0,
    .stack_mem  = 0,
    .stack_size = (8 * 1024), /* mbedTLS TLS1.2 + RSA-2048/PSA: measured ~1.2KB stack free before SKE verify (3KB stack). */
    .priority   = osPriorityAboveNormal,
    .tz_module  = 0,
};

typedef struct mqtt_demo_client_s
{
    Client client;
    uint32_t server_port;
    uint32_t client_port;
    sl_ip_address_t server_ip;
    uint16_t keep_alive_interval;
    int8_t * tcp_mqtt_tx_buffer;
    int8_t * tcp_mqtt_rx_buffer;
} mqtt_demo_client_t;

static int paho_mqtt_demo(void);
static void mqtt_lwip_thread(void * argument);

#if MQTT_USE_HOST_LWIP_TLS
static int configure_tls_certificates(TLS_cert_ctx_t * tls_config)
{
    tls_config->cacert     = (uint8_t *) cacert;
    tls_config->cacert_len = sizeof(cacert);
    return 0;
}
#endif

static void mqtt_log_network_connect_error(int status)
{
    if (status == NETWORK_ERROR_NULL_STRUCTURE)
    {
        SILABS_LOG(
            "MQTT error: network connect failed (%d); on dual-stack use paho_mqtt_embedded_dual_stack, not host lwIP sockets",
            status);
    }
    else if (status == NETWORK_ERROR_NULL_ADDRESS)
    {
        SILABS_LOG("MQTT error: broker address is NULL");
    }
    else if (status == NETWORK_ERROR_INVALID_TYPE)
    {
        SILABS_LOG("MQTT error: invalid MQTT transport type");
    }
#if defined(NETWORK_ERROR_TLS_HOSTNAME_REQUIRED)
    else if (status == NETWORK_ERROR_TLS_HOSTNAME_REQUIRED)
    {
        SILABS_LOG("MQTT error: TLS requires NetworkSetTlsHostname before connect");
    }
#endif
#if defined(NETWORK_ERROR_CONNECT_FAILED)
    else if (status == NETWORK_ERROR_CONNECT_FAILED)
    {
        SILABS_LOG("MQTT error: socket connect failed");
    }
#endif
    else
    {
        SILABS_LOG("MQTT error: TCP/TLS connect failed: %d", status);
    }
}

static void message_arrived(MessageData * md)
{
    if (md == NULL || md->message == NULL)
    {
        SILABS_LOG("MQTT error: received NULL message");
        return;
    }
    SILABS_LOG("MQTT message: %.*s", md->message->payloadlen, (char *) md->message->payload);
    halt = 1;
}

static int paho_mqtt_demo(void)
{
    int status;
    uint16_t flags                   = 0;
    mqtt_demo_client_t * mqtt_client = NULL;
    MQTTMessage publish_msg;
    int8_t * buffer_ptr = tcp_mqtt_client_buffer;

    mqtt_client = (mqtt_demo_client_t *) buffer_ptr;
    buffer_ptr += sizeof(mqtt_demo_client_t);
    mqtt_client->client.ipstack = (Network *) buffer_ptr;
    buffer_ptr += sizeof(Network);
    mqtt_client->server_port         = enable_ssl ? MQTTS_BROKER_PORT : MQTT_BROKER_PORT;
    mqtt_client->client_port         = MQTT_CLIENT_PORT;
    mqtt_client->keep_alive_interval = KEEP_ALIVE_PERIOD;
    mqtt_client->server_ip.type      = SL_IPV4;
    sl_net_inet_addr((char *) MQTT_BROKER_IP, (uint32_t *) &mqtt_client->server_ip.ip.v4.value);
    mqtt_client->tcp_mqtt_tx_buffer = buffer_ptr;
    buffer_ptr += TCP_MQTT_CLIENT_TX_BUFFER_SIZE;
    mqtt_client->tcp_mqtt_rx_buffer = buffer_ptr;
    buffer_ptr += TCP_MQTT_CLIENT_RX_BUFFER_SIZE;
    mqtt_client->client.ipstack->transport_type = MQTT_TRANSPORT_TCP;

    NetworkInit(mqtt_client->client.ipstack);

    MQTTClient((Client *) mqtt_client, mqtt_client->client.ipstack, TCP_MQTT_CONNECT_TIME_OUT,
               (uint8_t *) mqtt_client->tcp_mqtt_tx_buffer, TCP_MQTT_CLIENT_TX_BUFFER_SIZE,
               (uint8_t *) mqtt_client->tcp_mqtt_rx_buffer, TCP_MQTT_CLIENT_RX_BUFFER_SIZE);

    SILABS_LOG("MQTT connecting to broker %s port %lu", MQTT_BROKER_IP, (unsigned long) mqtt_client->server_port);
    SILABS_LOG("MQTT SSL enabled: %s", enable_ssl ? "yes" : "no");

#if MQTT_USE_HOST_LWIP_TLS
    if (enable_ssl)
    {
        mqtt_client->client.ipstack->tls = malloc(sizeof(mqtt_tls_context_t));
        if (!mqtt_client->client.ipstack->tls)
        {
            SILABS_LOG("MQTT error: failed to allocate TLS context");
            return -1;
        }
        configure_tls_certificates(&mqtt_client->client.ipstack->tls->cert_ctx);
        if (NetworkSetTlsHostname(mqtt_client->client.ipstack, MQTT_BROKER_TLS_HOSTNAME) != 0)
        {
            SILABS_LOG("MQTT error: invalid TLS hostname (MQTT_BROKER_TLS_HOSTNAME)");
            free(mqtt_client->client.ipstack->tls);
            mqtt_client->client.ipstack->tls = NULL;
            return -1;
        }
    }
#endif

    status = NetworkConnect(mqtt_client->client.ipstack, flags, (char *) &(mqtt_client->server_ip), mqtt_client->server_port,
                            mqtt_client->client_port, enable_ssl);

    if (status != 0)
    {
        mqtt_log_network_connect_error(status);
#if MQTT_USE_HOST_LWIP_TLS
        if (mqtt_client->client.ipstack->tls)
        {
            free(mqtt_client->client.ipstack->tls);
            mqtt_client->client.ipstack->tls = NULL;
        }
#endif
        return status;
    }
    SILABS_LOG("MQTT TCP/TLS connection established");

    MQTTPacket_connectData connectData = MQTTPacket_connectData_initializer;
    connectData.willFlag               = WILL_FLAG_ENABLE;
    connectData.will.topicName.cstring = LAST_WILL_TOPIC;
    connectData.will.message.cstring   = LAST_WILL_MESSAGE;
    connectData.will.qos               = QOS_OF_LAST_WILL;
    connectData.will.retained          = IS_LAST_WILL_RETAINED;
    connectData.MQTTVersion            = MQTT_VERSION;
    connectData.clientID.cstring       = (char *) clientID;
    connectData.username.cstring       = MQTT_USERNAME;
    connectData.password.cstring       = MQTT_PASSWORD;
    connectData.keepAliveInterval      = mqtt_client->keep_alive_interval;
    connectData.cleansession           = 1;

    status = MQTTConnect(&mqtt_client->client, &connectData);
    if (status != 0)
    {
        SILABS_LOG("MQTT error: MQTT connect failed: %d", status);
        NetworkDisconnect(mqtt_client->client.ipstack);
        return status;
    }
    SILABS_LOG("MQTT connected");

    status = MQTTSubscribe(&mqtt_client->client, (char *) TOPIC_TO_BE_SUBSCRIBED, (enum QoS) MQTT_QOS, message_arrived);
    if (status != 0)
    {
        SILABS_LOG("MQTT error: subscription failed: %d", status);
        NetworkDisconnect(mqtt_client->client.ipstack);
        return status;
    }
    SILABS_LOG("MQTT subscribed to topic: %s", TOPIC_TO_BE_SUBSCRIBED);

    publish_msg.dup = 0;
    if (MQTT_QOS == QOS0)
    {
        publish_msg.qos = QOS0;
    }
    else if (MQTT_QOS == QOS1)
    {
        publish_msg.qos = QOS1;
    }
    else
    {
        publish_msg.qos = QOS2;
    }
    publish_msg.retained   = 0;
    publish_msg.payload    = publish_message;
    publish_msg.payloadlen = strlen((char *) publish_message);

    status = MQTTPublish(&mqtt_client->client, (const char *) TOPIC_TO_BE_SUBSCRIBED, &publish_msg);
    if (status != 0)
    {
        SILABS_LOG("MQTT error: publish failed: %d", status);
        NetworkDisconnect(mqtt_client->client.ipstack);
        return status;
    }
    SILABS_LOG("MQTT published to topic successfully");
    SILABS_LOG("MQTT waiting for message on topic %s", TOPIC_TO_BE_SUBSCRIBED);

    while (!halt)
    {
        status = MQTTYield(&mqtt_client->client, 60000);
        if (status != SL_STATUS_OK)
        {
            SILABS_LOG("MQTT error: receive failed: 0x%X", status);
            NetworkDisconnect(mqtt_client->client.ipstack);
            return status;
        }
    }

    status = MQTTUnsubscribe(&mqtt_client->client, (const char *) TOPIC_TO_BE_SUBSCRIBED);
    if (status != SL_STATUS_OK)
    {
        SILABS_LOG("MQTT error: unsubscription failed: 0x%X", status);
        NetworkDisconnect(mqtt_client->client.ipstack);
        return status;
    }
    SILABS_LOG("MQTT unsubscribed");

    status = MQTTDisconnect(&mqtt_client->client);
    if (status != SL_STATUS_OK)
    {
        SILABS_LOG("MQTT error: disconnect failed: 0x%X", status);
        NetworkDisconnect(mqtt_client->client.ipstack);
        return status;
    }
    SILABS_LOG("MQTT disconnected");

    NetworkDisconnect(mqtt_client->client.ipstack);
    SILABS_LOG("MQTT demo completed");

    return 0;
}

static void mqtt_lwip_thread(void * argument)
{
    (void) argument;

    halt = 0;

    int result = paho_mqtt_demo();
    if (result == 0)
    {
        SILABS_LOG("MQTT demo succeeded");
    }
    else
    {
        SILABS_LOG("MQTT error: demo failed: %d", result);
    }

    s_mqtt_demo_thread_id = NULL;
    osThreadTerminate(osThreadGetId());
}

sl_status_t mqtt_client_demo_start(void)
{
    if (s_mqtt_demo_thread_id != NULL)
    {
        return SL_STATUS_ALREADY_INITIALIZED;
    }

    s_mqtt_demo_thread_id = osThreadNew((osThreadFunc_t) mqtt_lwip_thread, NULL, &mqtt_lwip_thread_attributes);
    if (s_mqtt_demo_thread_id == NULL)
    {
        return SL_STATUS_FAIL;
    }

    return SL_STATUS_OK;
}
