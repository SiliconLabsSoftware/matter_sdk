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

#include <cstddef>
#include <cstdint>

/** Shared MQTT topic and payload limits for the Matter MQTT bridge contract (v1). */
inline constexpr uint8_t MATTER_MQTT_CONTRACT_VERSION = 1;
inline constexpr size_t MATTER_MQTT_MAX_TOPIC_LEN     = 160;
inline constexpr size_t MATTER_MQTT_MAX_PAYLOAD_LEN   = 256;
inline constexpr size_t MATTER_MQTT_MAX_VALUE_LEN     = 64;
inline constexpr size_t MATTER_MQTT_MAX_ARGS          = 8;
inline constexpr size_t MATTER_MQTT_DEVICE_ID_LEN     = 20; // "0x" + 16 hex + NUL
inline constexpr size_t MATTER_MQTT_MAX_CMD_ARG_PACK  = 12;
inline constexpr size_t MATTER_MQTT_MAX_SNAPSHOT_PATHS = 32;

#ifndef SL_MATTER_MQTT_BRIDGE_POST_ATTRIBUTE_HOOK
#define SL_MATTER_MQTT_BRIDGE_POST_ATTRIBUTE_HOOK 0
#endif

#ifndef SL_MATTER_MQTT_BRIDGE_WILDCARD_SUBSCRIBE
#define SL_MATTER_MQTT_BRIDGE_WILDCARD_SUBSCRIBE 1
#endif
