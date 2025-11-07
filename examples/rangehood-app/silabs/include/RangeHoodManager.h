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
 * @class RangeHoodManager
 * @brief Manages the initialization and operations related to range hood
 *        extractor hood and light endpoints in the application.
 *
 * @note This class is part of the range hood application example
 */

#pragma once

#include "ExtractorHoodEndpoint.h"
#include "LightEndpoint.h"

#include "AppEvent.h"

#include <app-common/zap-generated/ids/Attributes.h>
#include <app/clusters/fan-control-server/fan-control-server.h>
#include <app/clusters/on-off-server/on-off-server.h>


using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters::FanControl;
using chip::EndpointId;
using chip::Percent;
using Protocols::InteractionModel::Status;

class RangeHoodManager
{
public:
    RangeHoodManager() :
        mState(kState_OffCompleted), mActionInitiated_CB(nullptr), mActionCompleted_CB(nullptr),
        mAutoTurnOffTimerArmed(false), mOffEffectArmed(false), mLightTimer(nullptr),
        mFanMode(FanModeEnum::kOff),
        // initialize endpoint helpers with the required EndpointIds:
        mExtractorHoodEndpoint1(kExtractorHoodEndpoint1), mLightEndpoint2(kLightEndpoint2)
    {}

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

    bool IsLightOn();
    void EnableAutoTurnOff(bool aOn);
    void SetAutoTurnOffDuration(uint32_t aDurationInSecs);
    bool IsActionInProgress();
    bool InitiateAction(int32_t aActor, Action_t aAction, uint8_t * aValue);
    typedef void (*Callback_fn_initiated)(Action_t, int32_t aActor, uint8_t * value);
    typedef void (*Callback_fn_completed)(Action_t);
    void SetCallbacks(Callback_fn_initiated aActionInitiated_CB, Callback_fn_completed aActionCompleted_CB);

    static void OnTriggerOffWithEffect(OnOffEffect * effect);

    /**
     * @brief Handle the step command from the Fan Control Cluster
     * @note This method delegates to FanDelegate::HandleStep for actual processing
     * @param endpointId The endpoint ID (currently unused, kept for API compatibility)
     * @param aDirection The step direction (increase/decrease)
     * @param aWrap Whether to wrap around at min/max
     * @param aLowestOff Whether lowest setting is considered off
     * @return Status Success on success, error code otherwise
     */
    Status ProcessExtractorStepCommand(chip::EndpointId endpointId, StepDirectionEnum aDirection, bool aWrap, bool aLowestOff);

    void HandleFanControlAttributeChange(AttributeId attributeId, uint8_t type, uint16_t size, uint8_t * value);

    void UpdateRangeHoodLCD();

    // Explicit getters for individual endpoints
    EndpointId GetExtractorEndpoint() { return kExtractorHoodEndpoint1; }
    EndpointId GetLightEndpoint() { return kLightEndpoint2; }

    /**
     * @brief Returns the singleton instance of the RangeHoodManager.
     *
     * @return Reference to the singleton RangeHoodManager instance.
     */
    static RangeHoodManager & GetInstance() { return sRangeHoodMgr; }

    /**
     * @brief Initializes the RangeHoodManager and its associated resources.
     *
     */
    CHIP_ERROR Init();

    // ...existing public methods...

private:
    friend RangeHoodManager & RangeHoodMgr(void);
    State_t mState;

    Callback_fn_initiated mActionInitiated_CB;
    Callback_fn_completed mActionCompleted_CB;

    bool mAutoTurnOffTimerArmed;  // Timer state managed by RangeHoodManager, auto-turn-off config in LightEndpoint
    bool mOffEffectArmed;
    osTimerId_t mLightTimer;

    void CancelTimer(void);
    void StartTimer(uint32_t aTimeoutMs);

    static void TimerEventHandler(void * timerCbArg);
    static void AutoTurnOffTimerEventHandler(AppEvent * aEvent);
    static void ActuatorMovementTimerEventHandler(AppEvent * aEvent);
    static void OffEffectTimerEventHandler(AppEvent * aEvent);

    FanModeEnum mFanMode;  // Cached fan mode for quick access (also available via endpoint GetFanMode)

    // Fan Mode Percent Mappings are now in ExtractorHoodEndpoint
    // Step command constants are now in ExtractorHoodEndpoint

    static RangeHoodManager sRangeHoodMgr;

    // Define the endpoint IDs for the RangeHood
    static constexpr chip::EndpointId kExtractorHoodEndpoint1 = 1;
    static constexpr chip::EndpointId kLightEndpoint2         = 2;

    chip::app::Clusters::ExtractorHood::ExtractorHoodEndpoint mExtractorHoodEndpoint1;
    chip::app::Clusters::Light::LightEndpoint mLightEndpoint2;
};

inline RangeHoodManager & RangeHoodMgr(void)
{
    return RangeHoodManager::sRangeHoodMgr;
}
