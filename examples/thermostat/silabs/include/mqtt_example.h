/***************************************************************************/
/**
 * @file mqtt_example.h
 * @brief Host LwIP Paho MQTT/MQTTS demo entry point
 *******************************************************************************
 * SPDX-License-Identifier: Zlib
 ******************************************************************************/
#pragma once

#include "sl_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Load config + run MQTT demo on the MqttClient service thread (idempotent). */
sl_status_t mqtt_client_demo_start(void);

#ifdef __cplusplus
}
#endif
