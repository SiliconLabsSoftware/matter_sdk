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
/** Max JSON report/command scalar string (e.g. enum name or integer text). */
inline constexpr size_t MATTER_MQTT_MAX_VALUE_LEN = 64;
/**
 * Max JSON object keys in inbound command `"args"` (ArgsView items).
 * Platform v1 adapters expose at most two scalar fields per command (e.g. Thermostat SetpointRaiseLower).
 * Headroom covers future multi-field commands without resizing static scratch buffers.
 */
inline constexpr size_t MATTER_MQTT_MAX_ARGS = 8;
/** Max length of one inbound JSON arg name (NUL-terminated scratch in matter_mqtt_bridge.cpp). */
inline constexpr size_t MATTER_MQTT_MAX_ARG_NAME_LEN = 32;
/** Inbound arg JSON text uses the same bound as outbound report scalars. */
inline constexpr size_t MATTER_MQTT_MAX_ARG_VALUE_LEN = MATTER_MQTT_MAX_VALUE_LEN;
/** Max MQTT topic device id: up to 16 uppercase hex digits (uint64 NodeId) plus NUL; was 20 with a 2-char "0x" prefix. */
inline constexpr size_t MATTER_MQTT_DEVICE_ID_LEN = 18;
/** Topic segment parse scratch: longest id is 16 hex digits (uint64) plus NUL. */
inline constexpr size_t MATTER_MQTT_TOPIC_ID_HEX_SCRATCH = 17;
/**
 * Max packed numeric/bool bytes for one command (PackCommandArgs).
 * Two bytes per small-integer TLV field; 12 fits six fields with margin.
 */
inline constexpr size_t MATTER_MQTT_MAX_CMD_ARG_PACK = 12;
inline constexpr size_t MATTER_MQTT_MAX_SNAPSHOT_PATHS = 32;
/**
 * Stack buffer for invoke-structure TLV built before CodegenDataModelProvider::InvokeCommand.
 * Observed scalar-only commands encode to well under 32 B; 96 B leaves room for short UTF-8 strings in the same structure.
 */
inline constexpr size_t MATTER_MQTT_MAX_INVOKE_TLV = 96;
