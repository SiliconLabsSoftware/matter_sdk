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

#include "ExtractorHoodEndpoint.h"
#include "LightEndpoint.h"

#include "AppEvent.h"

#include <app-common/zap-generated/ids/Attributes.h>
#include <app/clusters/on-off-server/on-off-server.h>
#include <app/clusters/fan-control-server/fan-control-server.h>

#include <lib/core/DataModelTypes.h>
#include <app/data-model/Nullable.h>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters::FanControl;
using chip::Percent;
using chip::EndpointId;
using Protocols::InteractionModel::Status;

class RangeHoodManager
{
public:
     RangeHoodManager()
        : mState(kState_OffCompleted),
          mActionInitiated_CB(nullptr),
          mActionCompleted_CB(nullptr),
          mAutoTurnOff(false),
          mAutoTurnOffDuration(0),
          mAutoTurnOffTimerArmed(false),
          mOffEffectArmed(false),
          mLightTimer(nullptr),
          mFanMode(FanModeEnum::kOff),
          mSpeedMax(0),
          percentCurrent(0),
          speedCurrent(0),
          // initialize endpoint helpers with the required EndpointIds:
          mExtractorHoodEndpoint1(kExtractorHoodEndpoint1),
          mLightEndpoint2(kLightEndpoint2)
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
     * @brief Central handler for  delegate requests, Handle the step command from the Fan Control Cluster
     */
    Status ProcessExtractorStepCommand(chip::EndpointId endpointId, StepDirectionEnum aDirection, bool aWrap, bool aLowestOff);

    void HandleFanControlAttributeChange(AttributeId attributeId, uint8_t type, uint16_t size, uint8_t * value);

   void PercentSettingWriteCallback(uint8_t aNewPercentSetting);
    void SpeedSettingWriteCallback(uint8_t aNewSpeedSetting);
    void FanModeWriteCallback(FanModeEnum aNewFanMode);

    void SetPercentSetting(Percent aNewPercentSetting);

    void UpdateFanMode();

    static void UpdateClusterState(intptr_t arg);

    FanModeEnum GetFanMode();
    void UpdateRangeHoodLCD();

    // Explicit getters for individual endpoints
    EndpointId GetExtractorEndpoint() { return kExtractorHoodEndpoint1; }
    EndpointId GetLightEndpoint() { return kLightEndpoint2; }

   struct AttributeUpdateInfo
    {
        FanModeEnum fanMode;
        uint8_t speedCurrent;
        uint8_t percentCurrent;
        uint8_t speedSetting;
        uint8_t percentSetting;
        bool isPercentCurrent = false;
        bool isSpeedCurrent   = false;
        bool isSpeedSetting   = false;
        bool isFanMode        = false;
        bool isPercentSetting = false;
        EndpointId endPoint;
    };

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

    bool mAutoTurnOff;
    uint32_t mAutoTurnOffDuration;
    bool mAutoTurnOffTimerArmed;
    bool mOffEffectArmed;
    osTimerId_t mLightTimer;

    void CancelTimer(void);
    void StartTimer(uint32_t aTimeoutMs);

    static void TimerEventHandler(void * timerCbArg);
    static void AutoTurnOffTimerEventHandler(AppEvent * aEvent);
    static void ActuatorMovementTimerEventHandler(AppEvent * aEvent);
    static void OffEffectTimerEventHandler(AppEvent * aEvent);

    FanModeEnum mFanMode;
    uint8_t mSpeedMax;
    uint8_t percentCurrent;
    uint8_t speedCurrent;

    //Fan Mode Limits
    static constexpr int kFanModeLowLowerBound    = 1;
    static constexpr int kFanModeLowUpperBound    = 3;
    static constexpr int kFanModeMediumLowerBound = 4;
    static constexpr int kFanModeMediumUpperBound = 7;
    static constexpr int kFanModeHighLowerBound   = 8;
    static constexpr int kFanModeHighUpperBound   = 10;

    static constexpr int kaLowestOffTrue  = 0;
    static constexpr int kaLowestOffFalse = 1;

    DataModel::Nullable<Percent> GetPercentSetting();
    DataModel::Nullable<uint8_t> GetSpeedSetting();

    static RangeHoodManager sRangeHoodMgr;

    // FanControlDelegate object

    // Define the endpoint ID for the RangeHood
    static constexpr chip::EndpointId kExtractorHoodEndpoint1                         = 1;
    static constexpr chip::EndpointId kLightEndpoint2                                 = 2;

    chip::app::Clusters::ExtractorHood::ExtractorHoodEndpoint mExtractorHoodEndpoint1;
    chip::app::Clusters::Light::LightEndpoint mLightEndpoint2;
};

inline RangeHoodManager & RangeHoodMgr(void)
{
    return RangeHoodManager::sRangeHoodMgr;
}
