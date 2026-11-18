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

#if defined(ZCL_USING_DOOR_LOCK_CLUSTER_SERVER)

#include <matter_mqtt_cluster_filter.h>

#include <app-common/zap-generated/cluster-objects.h>

using namespace chip::app::Clusters;

namespace {

constexpr MatterMqttFilterAttributeEntry kDoorLockAttributes[] = {
    { DoorLock::Attributes::LockState::Id },
};

constexpr MatterMqttFilterCommandEntry kDoorLockCommands[] = {
    { DoorLock::Commands::LockDoor::Id, MatterMqttBridge::kCommandFlagTimed },
};

} // namespace

const MatterMqttClusterFilter kDoorLockMqttClusterFilter = { DoorLock::Id,
                                                             kDoorLockAttributes,
                                                             sizeof(kDoorLockAttributes) / sizeof(kDoorLockAttributes[0]),
                                                             kDoorLockCommands,
                                                             sizeof(kDoorLockCommands) / sizeof(kDoorLockCommands[0]) };

#endif // ZCL_USING_DOOR_LOCK_CLUSTER_SERVER
