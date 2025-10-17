/*
 *
 *    Copyright (c) 2025 Project CHIP Authors
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

#include <OvenModeDelegate.h>
#include <app/clusters/mode-base-server/mode-base-server.h>
#include <app/clusters/temperature-control-server/supported-temperature-levels-manager.h>
#include <lib/core/CHIPError.h>

namespace chip {
namespace app {
namespace Clusters {
namespace TemperatureControlledCabinet {

// Add TemperatureControlDelegate here

class TemperatureControlledCabinetEndpoint
{
public:
    TemperatureControlledCabinetEndpoint(EndpointId endpointId) :
        mEndpointId(endpointId), mOvenModeDelegate(endpointId), mOvenModeInstance(&mOvenModeDelegate, mEndpointId, OvenMode::Id, 0)
    {}

    /**
     * Initialize the temperature controlled cabinet endpoint.
     */
    CHIP_ERROR Init();

private:
    EndpointId mEndpointId = kInvalidEndpointId;
    OvenModeDelegate mOvenModeDelegate;
    ModeBase::Instance mOvenModeInstance;
};

} // namespace TemperatureControlledCabinet
} // namespace Clusters
} // namespace app
} // namespace chip
