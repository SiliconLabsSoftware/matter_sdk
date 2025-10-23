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

/*
 * @class OvenManager
 * @brief Manages the initialization and operations related to oven and
 *        oven panel endpoints in the application.
 *
 * @note This class is part of the oven application example
 */

#pragma once

#include "CookSurfaceEndpoint.h"
#include "CookTopEndpoint.h"
#include "OvenEndpoint.h"
#include "TemperatureControlledCabinetEndpoint.h"
#include "AppSupportedTemperatureLevelsDelegate.h"
#include <lib/core/DataModelTypes.h>

class OvenManager
{
public:
    /**
     * @brief Initializes the OvenManager and its associated resources.
     *
     */
    void Init();

    /**
     * @brief Returns the singleton instance of the OvenManager.
     *
     * @return Reference to the singleton OvenManager instance.
     */
    static OvenManager & GetInstance() { return sOvenMgr; }

private:
    static OvenManager sOvenMgr;
    chip::app::Clusters::AppSupportedTemperatureLevelsDelegate mTemperatureControlDelegate;

    // Define the endpoint ID for the Oven
    static constexpr chip::EndpointId kOvenEndpoint1                         = 1;
    static constexpr chip::EndpointId kTemperatureControlledCabinetEndpoint2 = 2;
    static constexpr chip::EndpointId kCookTopEndpoint3                      = 3;
    static constexpr chip::EndpointId kCookSurfaceEndpoint4                  = 4;
    static constexpr chip::EndpointId kCookSurfaceEndpoint5                  = 5;

    chip::app::Clusters::Oven::OvenEndpoint mOvenEndpoint1{ kOvenEndpoint1 };
    chip::app::Clusters::TemperatureControlledCabinet::TemperatureControlledCabinetEndpoint mTemperatureControlledCabinetEndpoint2{
        kTemperatureControlledCabinetEndpoint2
    };
    chip::app::Clusters::CookTop::CookTopEndpoint mCookTopEndpoint3{ kCookTopEndpoint3 };
    chip::app::Clusters::CookSurface::CookSurfaceEndpoint mCookSurfaceEndpoint4{ kCookSurfaceEndpoint4 };
    chip::app::Clusters::CookSurface::CookSurfaceEndpoint mCookSurfaceEndpoint5{ kCookSurfaceEndpoint5 };
};
