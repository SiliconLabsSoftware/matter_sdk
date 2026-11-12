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

#include <cluster_adapter.h>

#if defined(ZCL_USING_ON_OFF_CLUSTER_SERVER)
extern const ClusterAdapter kOnOffAdapter;
#endif
#if defined(ZCL_USING_THERMOSTAT_CLUSTER_SERVER)
extern const ClusterAdapter kThermostatAdapter;
#endif
#if defined(ZCL_USING_DOOR_LOCK_CLUSTER_SERVER)
extern const ClusterAdapter kDoorLockAdapter;
#endif
#if defined(ZCL_USING_WINDOW_COVERING_CLUSTER_SERVER)
extern const ClusterAdapter kWindowCoveringAdapter;
#endif

namespace {

const ClusterAdapter kPlatformMqttAdapters[] = {
#if defined(ZCL_USING_ON_OFF_CLUSTER_SERVER)
    kOnOffAdapter,
#endif
#if defined(ZCL_USING_THERMOSTAT_CLUSTER_SERVER)
    kThermostatAdapter,
#endif
#if defined(ZCL_USING_DOOR_LOCK_CLUSTER_SERVER)
    kDoorLockAdapter,
#endif
#if defined(ZCL_USING_WINDOW_COVERING_CLUSTER_SERVER)
    kWindowCoveringAdapter,
#endif
};

} // namespace

chip::Span<const ClusterAdapter> GetPlatformMqttAdapters()
{
    return chip::Span<const ClusterAdapter>(kPlatformMqttAdapters, sizeof(kPlatformMqttAdapters) / sizeof(kPlatformMqttAdapters[0]));
}
