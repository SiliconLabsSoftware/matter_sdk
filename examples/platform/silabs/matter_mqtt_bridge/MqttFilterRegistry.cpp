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

#include <matter_mqtt_cluster_filter.h>
#include <zap-generated/gen_config.h>

#if defined(ZCL_USING_ON_OFF_CLUSTER_SERVER)
#include "filters/OnOffMqttFilter.h"
#endif
#if defined(ZCL_USING_THERMOSTAT_CLUSTER_SERVER)
#include "filters/ThermostatMqttFilter.h"
#endif
#if defined(ZCL_USING_DOOR_LOCK_CLUSTER_SERVER)
#include "filters/DoorLockMqttFilter.h"
#endif
#if defined(ZCL_USING_WINDOW_COVERING_CLUSTER_SERVER)
#include "filters/WindowCoveringMqttFilter.h"
#endif

namespace {

const MatterMqttClusterFilter kPlatformMqttClusterFilters[] = {
#if defined(ZCL_USING_ON_OFF_CLUSTER_SERVER)
    kOnOffMqttClusterFilter,
#endif
#if defined(ZCL_USING_THERMOSTAT_CLUSTER_SERVER)
    kThermostatMqttClusterFilter,
#endif
#if defined(ZCL_USING_DOOR_LOCK_CLUSTER_SERVER)
    kDoorLockMqttClusterFilter,
#endif
#if defined(ZCL_USING_WINDOW_COVERING_CLUSTER_SERVER)
    kWindowCoveringMqttClusterFilter,
#endif
};

} // namespace

chip::Span<const MatterMqttClusterFilter> GetPlatformMqttClusterFilters()
{
    return chip::Span<const MatterMqttClusterFilter>(kPlatformMqttClusterFilters,
                                                     sizeof(kPlatformMqttClusterFilters) / sizeof(kPlatformMqttClusterFilters[0]));
}
