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

#if defined(ZCL_USING_WINDOW_COVERING_CLUSTER_SERVER)

#include "ClusterAdapter.h"

#include <app-common/zap-generated/cluster-objects.h>

using namespace chip::app::Clusters;

namespace {

constexpr MatterMqttAttributeEntry kWindowAttributes[] = {
    { "CurrentPositionLiftPercent100ths", WindowCovering::Attributes::CurrentPositionLiftPercent100ths::Id,
      MatterMqttAttributeQuantity::kPercent100ths },
    { "CurrentPositionTiltPercent100ths", WindowCovering::Attributes::CurrentPositionTiltPercent100ths::Id,
      MatterMqttAttributeQuantity::kPercent100ths },
};

constexpr MatterMqttArgField kPercentArgs[] = {
    { "liftPercent100thsValue", 0, chip::TLV::TLVType::kTLVType_UnsignedInteger },
};

constexpr MatterMqttCommandEntry kWindowCommands[] = {
    { "GoToLiftPercentage", nullptr, WindowCovering::Commands::GoToLiftPercentage::Id, MatterMqttBridge::kCommandFlagNone,
      kPercentArgs, sizeof(kPercentArgs) / sizeof(kPercentArgs[0]) },
    { "GoToTiltPercentage", nullptr, WindowCovering::Commands::GoToTiltPercentage::Id, MatterMqttBridge::kCommandFlagNone,
      kPercentArgs, sizeof(kPercentArgs) / sizeof(kPercentArgs[0]) },
};

} // namespace

const ClusterAdapter kWindowCoveringAdapter = {
    WindowCovering::Id,
    "WindowCovering",
    kWindowAttributes,
    sizeof(kWindowAttributes) / sizeof(kWindowAttributes[0]),
    kWindowCommands,
    sizeof(kWindowCommands) / sizeof(kWindowCommands[0]),
    nullptr,
};

#endif // ZCL_USING_WINDOW_COVERING_CLUSTER_SERVER
