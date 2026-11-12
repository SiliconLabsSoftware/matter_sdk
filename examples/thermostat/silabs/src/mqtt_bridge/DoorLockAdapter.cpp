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

#if defined(ZCL_USING_DOOR_LOCK_CLUSTER_SERVER)

#include "ClusterAdapter.h"

#include <app-common/zap-generated/cluster-objects.h>

using namespace chip::app::Clusters;

namespace {

const char * DoorLockLockStateName(chip::AttributeId attrId, uint32_t raw)
{
    if (attrId != DoorLock::Attributes::LockState::Id)
    {
        return nullptr;
    }
    switch (static_cast<DoorLock::DlLockState>(raw))
    {
    case DoorLock::DlLockState::kNotFullyLocked:
        return "NotFullyLocked";
    case DoorLock::DlLockState::kLocked:
        return "Locked";
    case DoorLock::DlLockState::kUnlocked:
        return "Unlocked";
    case DoorLock::DlLockState::kUnlatched:
        return "Unlatched";
    default:
        return "Unknown";
    }
}

constexpr MatterMqttAttributeEntry kDoorLockAttributes[] = {
    { "LockState", DoorLock::Attributes::LockState::Id, MatterMqttAttributeQuantity::kEnum },
};

constexpr MatterMqttArgField kDoorLockPinArgs[] = {
    { "PINCode", 0, chip::TLV::TLVType::kTLVType_UTF8String, true },
};

constexpr MatterMqttCommandEntry kDoorLockCommands[] = {
    { "LockDoor", nullptr, DoorLock::Commands::LockDoor::Id, MatterMqttBridge::kCommandFlagTimed, kDoorLockPinArgs,
      sizeof(kDoorLockPinArgs) / sizeof(kDoorLockPinArgs[0]) },
    { "UnlockDoor", nullptr, DoorLock::Commands::UnlockDoor::Id, MatterMqttBridge::kCommandFlagTimed, kDoorLockPinArgs,
      sizeof(kDoorLockPinArgs) / sizeof(kDoorLockPinArgs[0]) },
};

} // namespace

const ClusterAdapter kDoorLockAdapter = {
    DoorLock::Id,
    "DoorLock",
    kDoorLockAttributes,
    sizeof(kDoorLockAttributes) / sizeof(kDoorLockAttributes[0]),
    kDoorLockCommands,
    sizeof(kDoorLockCommands) / sizeof(kDoorLockCommands[0]),
    DoorLockLockStateName,
};

#endif // ZCL_USING_DOOR_LOCK_CLUSTER_SERVER
