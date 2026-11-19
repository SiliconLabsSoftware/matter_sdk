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

#if defined(ZCL_USING_ON_OFF_CLUSTER_SERVER)

#include <MatterMqttClusterFilter.h>

#include <app-common/zap-generated/cluster-objects.h>

using namespace chip::app::Clusters;

namespace {

constexpr MatterMqttAttributeFilter kOnOffAttributes[] = {
    { OnOff::Attributes::OnOff::Id },
};

constexpr MatterMqttCommandFilter kOnOffCommands[] = {
    { OnOff::Commands::On::Id },
    { OnOff::Commands::Off::Id },
    { OnOff::Commands::Toggle::Id },
};

} // namespace

const MatterMqttClusterFilter kOnOffMqttClusterFilter = { OnOff::Id,
                                                         kOnOffAttributes,
                                                         sizeof(kOnOffAttributes) / sizeof(kOnOffAttributes[0]),
                                                         kOnOffCommands,
                                                         sizeof(kOnOffCommands) / sizeof(kOnOffCommands[0]) };

#endif // ZCL_USING_ON_OFF_CLUSTER_SERVER
