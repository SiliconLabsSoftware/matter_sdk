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
/** Max JSON report/command scalar string (decimal or bool text). */
inline constexpr size_t MATTER_MQTT_MAX_VALUE_LEN = 64;
/**
 * NUL-terminated MQTT report value in ReportWork (not a second copy of endpoint/cluster/attribute ids).
 * INT32_MIN as decimal needs 11 chars; +1 NUL. With path ids in ReportWork, sizeof(ReportWork) <= kWorkDataSize (32).
 */
inline constexpr size_t MATTER_MQTT_REPORT_WORK_VALUE_LEN = 12;
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
/** NUL-terminated device id and stack scratch for one topic hex segment (max 16 uint64 hex digits + NUL). */
inline constexpr size_t MATTER_MQTT_DEVICE_ID_LEN = 17;
inline constexpr size_t MATTER_MQTT_MAX_SNAPSHOT_PATHS = 32;
/**
 * Stack buffer for invoke-structure TLV built before CodegenDataModelProvider::InvokeCommand.
 * Observed scalar-only commands encode to well under 32 B; 96 B leaves room for short UTF-8 strings in the same structure.
 */
inline constexpr size_t MATTER_MQTT_MAX_INVOKE_TLV = 96;
