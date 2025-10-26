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
 * @brief Manages the initialization and operations related to oven,
 *        temperature controlled cabinet, cook top, and cook surface endpoints
 *        in the application.
 *
 * @note This class is part of the oven application example
 */

#pragma once

#include "AppSupportedTemperatureLevelsDelegate.h"
// Corrected relative paths (silabs/include -> go up two levels to oven-app root)
#include "OvenEndpoint.h"
#include "CookTopEndpoint.h"
#include "CookSurfaceEndpoint.h"

#include "AppEvent.h"

#include <app-common/zap-generated/ids/Attributes.h>
#include <app/clusters/on-off-server/on-off-server.h>
#include <lib/core/DataModelTypes.h>

class OvenManager
{
public:
    enum Action_t
    {
        ON_ACTION = 0,
        OFF_ACTION,

        INVALID_ACTION
    } Action;

    enum State_t
    {
        kState_OffInitiated = 0,
        kState_OffCompleted,
        kState_OnInitiated,
        kState_OnCompleted,
    } State;

    bool InitiateAction(int32_t aActor, Action_t aAction, uint8_t * aValue);
    typedef void (*Callback_fn_initiated)(Action_t, int32_t aActor, uint8_t * value);
    typedef void (*Callback_fn_completed)(Action_t);
    void SetCallbacks(Callback_fn_initiated aActionInitiated_CB, Callback_fn_completed aActionCompleted_CB);

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

    CHIP_ERROR SetCookSurfaceInitialState(chip::EndpointId cookSurfaceEndpoint);

    CHIP_ERROR SetTemperatureControlledCabinetInitialState(chip::EndpointId temperatureControlledCabinetEndpoint);
    void TempCtrlAttributeChangeHandler(chip::EndpointId endpointId, chip::AttributeId attributeId, uint8_t * value, uint16_t size);
    void OnOffAttributeChangeHandler(chip::EndpointId endpointId, chip::AttributeId attributeId, uint8_t * value, uint16_t size);

private:
    static OvenManager sOvenMgr;
    chip::app::Clusters::AppSupportedTemperatureLevelsDelegate mTemperatureControlDelegate;

    State_t mState;

    Callback_fn_initiated mActionInitiated_CB;
    Callback_fn_completed mActionCompleted_CB;

    static void ActuatorMovementHandler(AppEvent * aEvent);

    // Define the endpoint ID for the Oven
    static constexpr chip::EndpointId kOvenEndpoint                         = 1;
    static constexpr chip::EndpointId kTemperatureControlledCabinetEndpoint = 2;
    static constexpr chip::EndpointId kCookTopEndpoint                      = 3;
    static constexpr chip::EndpointId kCookSurfaceEndpoint1                 = 4;
    static constexpr chip::EndpointId kCookSurfaceEndpoint2                 = 5;

    chip::app::Clusters::Oven::OvenEndpoint mOvenEndpoint;
    chip::app::Clusters::TemperatureControlledCabinet::TemperatureControlledCabinetEndpoint mTemperatureControlledCabinetEndpoint{
        kTemperatureControlledCabinetEndpoint
    };
    chip::app::Clusters::CookTop::CookTopEndpoint mCookTopEndpoint{ kCookTopEndpoint };
    chip::app::Clusters::CookSurface::CookSurfaceEndpoint mCookSurfaceEndpoint1{ kCookSurfaceEndpoint1 };
    chip::app::Clusters::CookSurface::CookSurfaceEndpoint mCookSurfaceEndpoint2{ kCookSurfaceEndpoint2 };
};
