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
 *
 * @brief Host LwIP Paho MQTT/MQTTS demo entry point
 * 1. Init the MqttClient
 * 2. Connect to the MQTT broker
 * 3. Subscribe to the topic
 * 4. Publish the message
 * 5. Disconnect from the MQTT broker (optional)
 * 6. Deinit the MqttClient (optional)
 */

#pragma once

#include "sl_status.h"

#ifndef MQTT_BROKER_IP
#error "MQTT_BROKER_IP is not defined"
#endif

#ifndef MQTT_TLS_HOSTNAME // The TLS hostname of your MQTT broker
#error "MQTT_TLS_HOSTNAME is not defined"
#endif

#ifndef MQTT_BROKER_PORT // The port of your MQTT broker
#error "MQTT_BROKER_PORT is not defined"
#endif

#ifndef MQTT_CLIENT_PORT // The port of your MQTT client
#define MQTT_CLIENT_PORT 5003
#endif

#ifndef MQTT_CLIENT_ID // The ID of your MQTT client
#define MQTT_CLIENT_ID "WISECONNECT_SDK_TOPIC"
#endif

#ifndef MQTT_USERNAME // The username of your MQTT client
#define MQTT_USERNAME "john"
#endif

#ifndef MQTT_PASSWORD // The password of your MQTT client
#define MQTT_PASSWORD "doe"
#endif

#ifndef MQTT_TOPIC // The topic of your MQTT client
#define MQTT_TOPIC "THERMOSTAT-DATA"
#endif

#ifndef MQTT_PUBLISH_MESSAGE // The message to publish to the MQTT broker
#define MQTT_PUBLISH_MESSAGE "THIS IS MQTT CLIENT DEMO FROM APPLICATION"
#endif

/** Load config + run MQTT demo on the MqttClient service thread (idempotent). */
sl_status_t mqtt_client_demo_start(void);
