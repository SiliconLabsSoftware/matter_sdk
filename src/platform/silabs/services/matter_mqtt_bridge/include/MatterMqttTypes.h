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

#include "MatterMqttContract.h"

#include <lib/core/DataModelTypes.h>
#include <platform/silabs/services/mqtt_client.h>

#include <cstddef>
#include <cstdint>

/** Cross-thread work payload types for the Matter MQTT bridge. */
namespace MatterMqttBridge {

/**
 * MQTT-thread work payload for attribute reports. NodeId is omitted: attribute/command topics use
 * DeviceMqttProfile when building/parsing MQTT topics (bind time), not when queuing this struct.
 */
struct ReportWork
{
    chip::EndpointId endpoint     = 0;
    chip::ClusterId clusterId     = 0;
    chip::AttributeId attributeId = 0;
    char value[MATTER_MQTT_REPORT_WORK_VALUE_LEN]{};
};

static_assert(sizeof(ReportWork) <= chip::DeviceLayer::Silabs::MqttClient::kWorkDataSize,
              "ReportWork must fit MqttClient::ScheduleWork payload");

} // namespace MatterMqttBridge