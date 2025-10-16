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

#include "OvenManager.h"
#include "TemperatureControlledCabinetEndpoint.h"
#include "OvenModeDelegate.h"
#include <app-common/zap-generated/cluster-enums.h>
#include <app-common/zap-generated/cluster-objects.h>
#include <app-common/zap-generated/attributes/Accessors.h>
#include <app/clusters/temperature-control-server/supported-temperature-levels-manager.h>
#include <protocols/interaction_model/StatusCode.h>

using namespace chip;
using namespace chip::app::Clusters::TemperatureControlledCabinet;
using namespace chip::app::DataModel;
using namespace chip::app::Clusters;

using Protocols::InteractionModel::Status;

CHIP_ERROR TemperatureControlledCabinetEndpoint::Init()
{
    // Initialize the Oven Mode instance and delegate
    ReturnErrorOnFailure(mOvenModeInstance.Init());
    ReturnErrorOnFailure(mOvenModeDelegate.Init());
    
    // Set the TemperatureControl cluster min and max temperature values
    // Temperature values are in hundredths of degrees Celsius (0°C = 0, 100°C = 10000)
    using namespace chip::app::Clusters::TemperatureControl::Attributes;
    
    auto status = MinTemperature::Set(mEndpointId, 0);  // 0°C
    if (status != chip::Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(AppServer, "Failed to set MinTemperature: %d", static_cast<int>(status));
        return CHIP_ERROR_INTERNAL;
    }
    
    status = MaxTemperature::Set(mEndpointId, 10000);  // 100°C  
    if (status != chip::Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(AppServer, "Failed to set MaxTemperature: %d", static_cast<int>(status));
        return CHIP_ERROR_INTERNAL;
    }
    
    // Set temperature step to 5°C (500 in hundredths of degrees)
    status = Step::Set(mEndpointId, 500);  // 5°C step
    if (status != chip::Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(AppServer, "Failed to set temperature Step: %d", static_cast<int>(status));
        return CHIP_ERROR_INTERNAL;
    }
    
    ChipLogProgress(AppServer, "TemperatureControlledCabinetEndpoint initialized with MinTemperature=0°C, MaxTemperature=100°C, Step=5°C");
    
    return CHIP_NO_ERROR;
}
