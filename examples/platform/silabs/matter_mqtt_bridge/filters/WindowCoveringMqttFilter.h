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

#if defined(ZCL_USING_WINDOW_COVERING_CLUSTER_SERVER)

#include <matter_mqtt_cluster_filter.h>

#include <app-common/zap-generated/cluster-objects.h>

using namespace chip::app::Clusters;

namespace {

constexpr MatterMqttFilterAttributeEntry kWindowAttributes[] = {
    { WindowCovering::Attributes::CurrentPositionLiftPercent100ths::Id },
    { WindowCovering::Attributes::CurrentPositionTiltPercent100ths::Id },
};

constexpr MatterMqttFilterCommandEntry kWindowCommands[] = {
    { WindowCovering::Commands::GoToLiftPercentage::Id, MatterMqttBridge::kCommandFlagNone },
    { WindowCovering::Commands::GoToTiltPercentage::Id, MatterMqttBridge::kCommandFlagNone },
};

} // namespace

const MatterMqttClusterFilter kWindowCoveringMqttClusterFilter = { WindowCovering::Id,
                                                                   kWindowAttributes,
                                                                   sizeof(kWindowAttributes) / sizeof(kWindowAttributes[0]),
                                                                   kWindowCommands,
                                                                   sizeof(kWindowCommands) / sizeof(kWindowCommands[0]) };

#endif // ZCL_USING_WINDOW_COVERING_CLUSTER_SERVER
