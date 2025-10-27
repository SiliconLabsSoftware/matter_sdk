/*
 *
 *    Copyright (c) 2024 Project CHIP Authors
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

#include "RangeHoodManager.h"

#include "ExtractorHoodEndpoint.h"
#include "LightEndpoint.h"

#include "AppConfig.h"
#include "AppTask.h"

#include <app-common/zap-generated/attributes/Accessors.h>
#include <platform/CHIPDeviceLayer.h>

using namespace chip;
using namespace chip::app;
using namespace ::chip::app::Clusters;
using Protocols::InteractionModel::Status;

RangeHoodManager RangeHoodManager::sRangeHoodMgr;

CHIP_ERROR RangeHoodManager::Init()
{
    DeviceLayer::PlatformMgr().LockChipStack();
    // Endpoint initializations
    VerifyOrReturn(mExtractorHoodEndpoint1.Init() == CHIP_NO_ERROR, ChipLogError(AppServer, "mExtractorHoodEndpoint1 Init failed"));

    VerifyOrReturn(mLightEndpoint2.Init() == CHIP_NO_ERROR,
                   ChipLogError(AppServer, "mLightEndpoint2 Init failed"));

        // Create cmsis os sw timer for light timer.
    mLightTimer = osTimerNew(TimerEventHandler, // timer callback handler
                             osTimerOnce,       // no timer reload (one-shot timer)
                             (void *) this,     // pass the app task obj context
                             NULL               // No osTimerAttr_t to provide.
    );

    if (mLightTimer == NULL)
    {
        SILABS_LOG("mLightTimer timer create failed");
        return APP_ERROR_CREATE_TIMER_FAILED;
    }

    bool currentLedState;

    // read current on/off value on light endpoint. 
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    OnOffServer::Instance().getOnOffValue(LIGHT_ENDPOINT, &currentLedState);


    Attributes::SpeedMax::Get(kExtractorHoodEndpoint1, &mSpeedMax);

    DataModel::Nullable<Percent> percentSettingNullable = GetPercentSetting();
    uint8_t percentSettingCB                            = percentSettingNullable.IsNull() ? 0 : percentSettingNullable.Value();
    PercentSettingWriteCallback(percentSettingCB);

    DataModel::Nullable<uint8_t> speedSettingNullable = GetSpeedSetting();
    uint8_t speedSettingCB                            = speedSettingNullable.IsNull() ? 0 : speedSettingNullable.Value();
    SpeedSettingWriteCallback(speedSettingCB);

    FanControl::SetDefaultDelegate(kExtractorHoodEndpoint1, &mExtractorHoodEndpoint1.GetFanDelegate());

    DeviceLayer::PlatformMgr().UnlockChipStack();
    
    return CHIP_NO_ERROR;
}

bool RangeHoodManager::InitiateAction(int32_t aActor, Action_t aAction, uint8_t * aValue)
{
    bool action_initiated = false;
    State_t new_state;

    // Initiate Turn On/Off Action only when the previous one is complete.
    if (((mState == kState_OffCompleted) || mOffEffectArmed) && aAction == ON_ACTION)
    {
        action_initiated = true;

        new_state = kState_OnInitiated;
        if (mOffEffectArmed)
        {
            CancelTimer();
            mOffEffectArmed = false;
        }
    }
    else if (mState == kState_OnCompleted && aAction == OFF_ACTION && mOffEffectArmed == false)
    {
        action_initiated = true;

        new_state = kState_OffInitiated;
        if (mAutoTurnOffTimerArmed)
        {
            // If auto turn off timer has been armed and someone initiates turning off,
            // cancel the timer and continue as normal.
            mAutoTurnOffTimerArmed = false;

            CancelTimer();
        }
    }

    if (action_initiated && (aAction == ON_ACTION || aAction == OFF_ACTION))
    {
        StartTimer(ACTUATOR_MOVEMENT_PERIOD_MS);
        mState = new_state;
    }

    if (action_initiated && mActionInitiated_CB)
    {
        mActionInitiated_CB(aAction, aActor, aValue);
    }

    return action_initiated;
}

void RangeHoodManager::StartTimer(uint32_t aTimeoutMs)
{
    // Starts or restarts the function timer
    if (osTimerStart(mLightTimer, pdMS_TO_TICKS(aTimeoutMs)) != osOK)
    {
        SILABS_LOG("mLightTimer timer start() failed");
        appError(APP_ERROR_START_TIMER_FAILED);
    }
}

void RangeHoodManager::CancelTimer(void)
{
    if (osTimerStop(mLightTimer) == osError)
    {
        SILABS_LOG("mLightTimer stop() failed");
        appError(APP_ERROR_STOP_TIMER_FAILED);
    }
}

void RangeHoodManager::TimerEventHandler(void * timerCbArg)
{
    // The callback argument is the light obj context assigned at timer creation.
    RangeHoodManager * light = static_cast<RangeHoodManager *>(timerCbArg);

    // The timer event handler will be called in the context of the timer task
    // once mLightTimer expires. Post an event to apptask queue with the actual handler
    // so that the event can be handled in the context of the apptask.
    AppEvent event;
    event.Type               = AppEvent::kEventType_Timer;
    event.TimerEvent.Context = light;
    if (light->mAutoTurnOffTimerArmed)
    {
        event.Handler = AutoTurnOffTimerEventHandler;
    }
    else if (light->mOffEffectArmed)
    {
        event.Handler = OffEffectTimerEventHandler;
    }
    else
    {
        event.Handler = ActuatorMovementTimerEventHandler;
    }
    AppTask::GetAppTask().PostEvent(&event);
}

void RangeHoodManager::AutoTurnOffTimerEventHandler(AppEvent * aEvent)
{
    RangeHoodManager * light = static_cast<RangeHoodManager *>(aEvent->TimerEvent.Context);
    int32_t actor           = AppEvent::kEventType_Timer;
    uint8_t value           = aEvent->LightEvent.Value;

    // Make sure auto turn off timer is still armed.
    if (!light->mAutoTurnOffTimerArmed)
    {
        return;
    }

    light->mAutoTurnOffTimerArmed = false;

    SILABS_LOG("Auto Turn Off has been triggered!");

    light->InitiateAction(actor, OFF_ACTION, &value);
}

void RangeHoodManager::OffEffectTimerEventHandler(AppEvent * aEvent)
{
    RangeHoodManager * light = static_cast<RangeHoodManager *>(aEvent->TimerEvent.Context);
    int32_t actor           = AppEvent::kEventType_Timer;
    uint8_t value           = aEvent->LightEvent.Value;

    // Make sure auto turn off timer is still armed.
    if (!light->mOffEffectArmed)
    {
        return;
    }

    light->mOffEffectArmed = false;

    SILABS_LOG("OffEffect completed");

    light->InitiateAction(actor, OFF_ACTION, &value);
}

void RangeHoodManager::ActuatorMovementTimerEventHandler(AppEvent * aEvent)
{
    Action_t actionCompleted = INVALID_ACTION;

    RangeHoodManager * light = static_cast<RangeHoodManager *>(aEvent->TimerEvent.Context);

    if (light->mState == kState_OffInitiated)
    {
        light->mState   = kState_OffCompleted;
        actionCompleted = OFF_ACTION;
    }
    else if (light->mState == kState_OnInitiated)
    {
        light->mState   = kState_OnCompleted;
        actionCompleted = ON_ACTION;
    }

    if (actionCompleted != INVALID_ACTION)
    {
        if (light->mActionCompleted_CB)
        {
            light->mActionCompleted_CB(actionCompleted);
        }

        if (light->mAutoTurnOff && actionCompleted == ON_ACTION)
        {
            // Start the timer for auto turn off
            light->StartTimer(light->mAutoTurnOffDuration * 1000);

            light->mAutoTurnOffTimerArmed = true;

            SILABS_LOG("Auto Turn off enabled. Will be triggered in %u seconds", light->mAutoTurnOffDuration);
        }
    }
}

void RangeHoodManager::OnTriggerOffWithEffect(OnOffEffect * effect)
{
    auto effectId              = effect->mEffectIdentifier;
    auto effectVariant         = effect->mEffectVariant;
    uint32_t offEffectDuration = 0;

    // Temporary print outs and delay to test OffEffect behaviour
    // Until dimming is supported for dev boards.
    if (effectId == EffectIdentifierEnum::kDelayedAllOff)
    {
        auto typedEffectVariant = static_cast<DelayedAllOffEffectVariantEnum>(effectVariant);
        if (typedEffectVariant == DelayedAllOffEffectVariantEnum::kDelayedOffFastFade)
        {
            offEffectDuration = 800;
            ChipLogProgress(Zcl, "DelayedAllOffEffectVariantEnum::kDelayedOffFastFade");
        }
        else if (typedEffectVariant == DelayedAllOffEffectVariantEnum::kNoFade)
        {
            offEffectDuration = 800;
            ChipLogProgress(Zcl, "DelayedAllOffEffectVariantEnum::kNoFade");
        }
        else if (typedEffectVariant == DelayedAllOffEffectVariantEnum::kDelayedOffSlowFade)
        {
            offEffectDuration = 12800;
            ChipLogProgress(Zcl, "DelayedAllOffEffectVariantEnum::kDelayedOffSlowFade");
        }
    }
    else if (effectId == EffectIdentifierEnum::kDyingLight)
    {
        auto typedEffectVariant = static_cast<DyingLightEffectVariantEnum>(effectVariant);
        if (typedEffectVariant == DyingLightEffectVariantEnum::kDyingLightFadeOff)
        {
            offEffectDuration = 1500;
            ChipLogProgress(Zcl, "DyingLightEffectVariantEnum::kDyingLightFadeOff");
        }
    }

    this->mOffEffectArmed = true;
    this->StartTimer(offEffectDuration);
}
 
Status RangeHoodManager::ProcessExtractorStepCommand(chip::EndpointId endpointId, StepDirectionEnum aDirection, bool aWrap, bool aLowestOff)
{
    ChipLogProgress(AppServer, "RangeHoodManager::ProcessExtractorStepCommand  ep=%u  aDirection %d, aWrap %d, aLowestOff %d", endpointId, to_underlying(aDirection),
                    aWrap, aLowestOff);

    VerifyOrReturnError(aDirection != StepDirectionEnum::kUnknownEnumValue, Status::InvalidCommand);

    // if aLowestOff is true, Step command can reduce the fan speed to 0, else 1.
    uint8_t speedMin = aLowestOff ? kaLowestOffTrue : kaLowestOffFalse;

    DataModel::Nullable<uint8_t> speedSettingNullable = GetSpeedSetting();

    uint8_t curSpeedSetting = speedSettingNullable.IsNull() ? speedMin : speedSettingNullable.Value();

    // increase or decrease the fan speed by one step
    switch (aDirection)
    {
    case StepDirectionEnum::kIncrease: {
        if (curSpeedSetting >= mSpeedMax)
        {
            curSpeedSetting = aWrap ? speedMin : mSpeedMax;
        }
        else
        {
            curSpeedSetting++;
        }
        break;
    }
    case StepDirectionEnum::kDecrease: {
        if (curSpeedSetting <= speedMin)
        {
            curSpeedSetting = aWrap ? mSpeedMax : speedMin;
        }
        else
        {
            curSpeedSetting--;
        }
        break;
    }
    default: {
        break;
    }
    }

    // Convert SpeedSetting to PercentSetting
    uint8_t curPercentSetting = ((static_cast<uint16_t>(curSpeedSetting) * 100) / mSpeedMax);

    AttributeUpdateInfo * data = chip::Platform::New<AttributeUpdateInfo>();
    data->percentSetting       = curPercentSetting;
    data->endPoint             = kExtractorHoodEndpoint1;
    data->isPercentSetting     = true;

    if (chip::DeviceLayer::PlatformMgr().ScheduleWork(UpdateClusterState, reinterpret_cast<intptr_t>(data)) != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "RangeHoodManager::HandleStep: Failed to update PercentSetting attribute");
        return Status::Failure;
    }

    return Status::Success;
}

void RangeHoodManager::UpdateClusterState(intptr_t arg)
{
    RangeHoodManager::AttributeUpdateInfo * data = reinterpret_cast<RangeHoodManager::AttributeUpdateInfo *>(arg);
    if (data->isSpeedCurrent)
    {
        Attributes::SpeedCurrent::Set(data->endPoint, data->speedCurrent);
    }
    else if (data->isPercentCurrent)
    {
        Attributes::PercentCurrent::Set(data->endPoint, data->percentCurrent);
    }
    else if (data->isSpeedSetting)
    {
        Attributes::SpeedSetting::Set(data->endPoint, data->speedSetting);
    }
    else if (data->isFanMode)
    {
        Attributes::FanMode::Set(data->endPoint, data->fanMode);
    }
    else if (data->isPercentSetting)
    {
        Attributes::PercentSetting::Set(data->endPoint, data->percentSetting);
    }
    chip::Platform::Delete(data);
}

void RangeHoodManager::HandleFanControlAttributeChange(AttributeId attributeId, uint8_t type, uint16_t size, uint8_t * value)
{
    switch (attributeId)
    {
    case Attributes::PercentSetting::Id: {
        PercentSettingWriteCallback(*value);
        break;
    }

    case Attributes::FanMode::Id: {
        mFanMode = *reinterpret_cast<FanModeEnum *>(value);
        FanModeWriteCallback(mFanMode);
        break;
    }

    default: {
        break;
    }
    }
}

void RangeHoodManager::PercentSettingWriteCallback(uint8_t aNewPercentSetting)
{
    VerifyOrReturn(aNewPercentSetting != percentCurrent);
    VerifyOrReturn(mFanMode != FanModeEnum::kAuto);
    ChipLogDetail(NotSpecified, "RangeHoodManager::PercentSettingWriteCallback: %d", aNewPercentSetting);
    percentCurrent = aNewPercentSetting;

    AttributeUpdateInfo * data = chip::Platform::New<AttributeUpdateInfo>();
    data->endPoint             = kExtractorHoodEndpoint1;
    data->percentCurrent       = percentCurrent;
    data->isPercentCurrent     = true;

    if (chip::DeviceLayer::PlatformMgr().ScheduleWork(UpdateClusterState, reinterpret_cast<intptr_t>(data)) != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "RangeHoodManager::PercentSettingWriteCallback: failed to set PercentCurrent attribute");
    }
}

void RangeHoodManager::SpeedSettingWriteCallback(uint8_t aNewSpeedSetting)
{
    VerifyOrReturn(aNewSpeedSetting != speedCurrent);
    VerifyOrReturn(mFanMode != FanModeEnum::kAuto);
    ChipLogDetail(NotSpecified, "RangeHoodManager::SpeedSettingWriteCallback: %d", aNewSpeedSetting);
    speedCurrent = aNewSpeedSetting;

    AttributeUpdateInfo * data = chip::Platform::New<AttributeUpdateInfo>();
    data->endPoint             = kExtractorHoodEndpoint1;
    data->speedCurrent         = speedCurrent;
    data->isSpeedCurrent       = true;

    if (chip::DeviceLayer::PlatformMgr().ScheduleWork(UpdateClusterState, reinterpret_cast<intptr_t>(data)) != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "RangeHoodManager::SpeedSettingWriteCallback: failed to set SpeedCurrent attribute");
    }

    // Update the fan mode as per the current speed.
    if (speedCurrent == 0)
    {
        mFanMode = FanModeEnum::kOff;
    }
    else if (speedCurrent <= kFanModeLowUpperBound)
    {
        mFanMode = FanModeEnum::kLow;
    }
    else if (speedCurrent <= kFanModeMediumUpperBound)
    {
        mFanMode = FanModeEnum::kMedium;
    }
    else if (speedCurrent <= kFanModeHighUpperBound)
    {
        mFanMode = FanModeEnum::kHigh;
    }
    UpdateFanMode();
}

void RangeHoodManager::UpdateFanMode()
{
    AttributeUpdateInfo * data = chip::Platform::New<AttributeUpdateInfo>();
    data->endPoint             = kExtractorHoodEndpoint1;
    data->fanMode              = mFanMode;
    data->isFanMode            = true;
    if (chip::DeviceLayer::PlatformMgr().ScheduleWork(UpdateClusterState, reinterpret_cast<intptr_t>(data)) != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "RangeHoodManager::UpdateFanMode: failed to update FanMode attribute");
    }
}

void RangeHoodManager::FanModeWriteCallback(FanModeEnum aNewFanMode)
{
    ChipLogDetail(NotSpecified, "RangeHoodManager::FanModeWriteCallback: %d", (uint8_t) aNewFanMode);
    // Set Percent Settings, which will update the Speed Settings through callback.
    switch (aNewFanMode)
    {
    case FanModeEnum::kOff: {
        if (speedCurrent != 0)
        {
            Percent percentSetting = 0;
            SetPercentSetting(percentSetting);
        }
        break;
    }
    case FanModeEnum::kLow: {
        if (speedCurrent < kFanModeLowLowerBound || speedCurrent > kFanModeLowUpperBound)
        {
            Percent percentSetting = kFanModeLowUpperBound * mSpeedMax;
            SetPercentSetting(percentSetting);
        }
        break;
    }
    case FanModeEnum::kMedium: {
        if (speedCurrent < kFanModeMediumLowerBound || speedCurrent > kFanModeMediumUpperBound)
        {
            Percent percentSetting = kFanModeMediumLowerBound * mSpeedMax;
            SetPercentSetting(percentSetting);
        }
        break;
    }
    case FanModeEnum::kOn:
    case FanModeEnum::kHigh: {
        if (speedCurrent < kFanModeHighLowerBound || speedCurrent > kFanModeHighUpperBound)
        {
            Percent percentSetting = kFanModeHighLowerBound * mSpeedMax;
            SetPercentSetting(percentSetting);
        }
        break;
    }
    case FanModeEnum::kSmart:
    case FanModeEnum::kAuto: {
        UpdateFanMode();
        break;
    }
    case FanModeEnum::kUnknownEnumValue: {
        ChipLogProgress(NotSpecified, "RangeHoodManager::FanModeWriteCallback: Unknown");
        break;
    }
    default:
        break;
    }
}

void RangeHoodManager::SetPercentSetting(Percent aNewPercentSetting)
{
    if (aNewPercentSetting != percentCurrent)
    {
        AttributeUpdateInfo * data = chip::Platform::New<AttributeUpdateInfo>();
        data->percentCurrent       = aNewPercentSetting;
        data->endPoint             = kExtractorHoodEndpoint1;
        data->isPercentCurrent     = true;
        if (chip::DeviceLayer::PlatformMgr().ScheduleWork(UpdateClusterState, reinterpret_cast<intptr_t>(data)) != CHIP_NO_ERROR)
        {
            ChipLogError(NotSpecified, "RangeHoodManager::SetPercentSetting: failed to update PercentSetting attribute");
        }
    }
}

DataModel::Nullable<uint8_t> RangeHoodManager::GetSpeedSetting()
{
    DataModel::Nullable<uint8_t> speedSetting;

    Status status = Attributes::SpeedSetting::Get(kExtractorHoodEndpoint1, speedSetting);
    if (status != Status::Success)
    {
        ChipLogError(NotSpecified, "RangeHoodManager::GetSpeedSetting: failed to get SpeedSetting attribute: %d",
                     to_underlying(status));
    }

    return speedSetting;
}

DataModel::Nullable<Percent> RangeHoodManager::GetPercentSetting()
{
    DataModel::Nullable<Percent> percentSetting;

    Status status = Attributes::PercentSetting::Get(kExtractorHoodEndpoint1, percentSetting);
    if (status != Status::Success)
    {
        ChipLogError(NotSpecified, "RangeHoodManager::GetPercentSetting: failed to get PercentSetting attribute: %d",
                     to_underlying(status));
    }

    return percentSetting;
}