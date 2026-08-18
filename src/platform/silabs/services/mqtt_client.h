/***************************************************************************/
/**
 * @file mqtt_client.h
 * @brief Host LwIP Paho MQTT/MQTTS demo entry point
 *******************************************************************************
 * SPDX-License-Identifier: Zlib
 ******************************************************************************/
#pragma once

#include "sl_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start the Paho MQTT over LwIP demo on a dedicated thread (idempotent). */
sl_status_t mqtt_client_demo_start(void);

#ifdef __cplusplus
}
#endif
