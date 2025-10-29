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
#include "CookSurfaceEndpoint.h"
#include "CookTopEndpoint.h"
#include "OvenEndpoint.h"

#include "AppEvent.h"

#include <app-common/zap-generated/ids/Attributes.h>
#include <app/clusters/mode-base-server/mode-base-cluster-objects.h>
#include <app/clusters/on-off-server/on-off-server.h>
#include <lib/core/DataModelTypes.h>
#include <lib/support/TypeTraits.h>
#include <platform/CHIPDeviceLayer.h>

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
        kCookTopState_OffInitiated = 0,
        kCookTopState_OffCompleted,
        kCookTopState_OnInitiated,
        kCookTopState_OnCompleted,

        // Cook Surface states
        kCookSurfaceState_OffInitiated,
        kCookSurfaceState_OffCompleted,
        kCookSurfaceState_OnInitiated,
        kCookSurfaceState_OnCompleted,
        kCookSurfaceState_ActionInProgress,
        kCookSurfaceState_NoAction,
    } State;


    bool InitiateAction(int32_t aActor, Action_t aAction, uint8_t * aValue, chip::EndpointId endpointId = kCookTopEndpoint);
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
    /**
     * @brief Handles temperature control attribute changes.
     *
     * @param endpointId The ID of the endpoint.
     * @param attributeId The ID of the attribute.
     * @param value Pointer to the new value.
     * @param size Size of the new value.
     */
    void TempCtrlAttributeChangeHandler(chip::EndpointId endpointId, chip::AttributeId attributeId, uint8_t * value, uint16_t size);

    /**
     * @brief Handles on/off attribute changes.
     *
     * @param endpointId The ID of the endpoint.
     * @param attributeId The ID of the attribute.
     * @param value Pointer to the new value.
     * @param size Size of the new value.
     */
    void OnOffAttributeChangeHandler(chip::EndpointId endpointId, chip::AttributeId attributeId, uint8_t * value, uint16_t size);

    /**
     * @brief Handles oven mode attribute changes.
     *
     * @param endpointId The ID of the endpoint.
     * @param attributeId The ID of the attribute.
     * @param value Pointer to the new value.
     * @param size Size of the new value.
     */
    void OvenModeAttributeChangeHandler(chip::EndpointId endpointId, chip::AttributeId attributeId, uint8_t * value, uint16_t size);

    /**
     * @brief Checks if a transition between two oven modes is blocked.
     *
     * @param fromMode The current mode.
     * @param toMode The desired mode.
     * @return True if the transition is blocked, false otherwise.
     */
    bool IsTransitionBlocked(uint8_t fromMode, uint8_t toMode);

private:
    struct BlockedTransition
    {
        uint8_t fromMode;
        uint8_t toMode;
    };

    // Disallowed OvenMode Transitions.
    static constexpr BlockedTransition kBlockedTransitions[3] = {
        { chip::to_underlying(chip::app::Clusters::TemperatureControlledCabinet::OvenModeDelegate::OvenModes::kModeGrill),
        chip::to_underlying(chip::app::Clusters::TemperatureControlledCabinet::OvenModeDelegate::OvenModes::kModeProofing) },
        { chip::to_underlying(chip::app::Clusters::TemperatureControlledCabinet::OvenModeDelegate::OvenModes::kModeProofing),
        chip::to_underlying(chip::app::Clusters::TemperatureControlledCabinet::OvenModeDelegate::OvenModes::kModeClean) },
        { chip::to_underlying(chip::app::Clusters::TemperatureControlledCabinet::OvenModeDelegate::OvenModes::kModeClean),
        chip::to_underlying(chip::app::Clusters::TemperatureControlledCabinet::OvenModeDelegate::OvenModes::kModeBake) },
    };

    static OvenManager sOvenMgr;
    chip::app::Clusters::AppSupportedTemperatureLevelsDelegate mTemperatureControlDelegate;

    State_t mCookTopState;
    State_t mCookSurfaceState1;
    State_t mCookSurfaceState2;

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
