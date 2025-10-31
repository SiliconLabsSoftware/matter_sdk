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
#include <app/clusters/fan-control-server/fan-control-server.h>
#include <platform/CHIPDeviceLayer.h>

#if DISPLAY_ENABLED
#include "RangeHoodUI.h"
#endif

using namespace chip;
using namespace chip::app;
using namespace ::chip::app::Clusters;
using namespace ::chip::app::Clusters::FanControl;

using namespace ::chip::app::Clusters::OnOff;
using Protocols::InteractionModel::Status;
using namespace ::chip::DeviceLayer;

RangeHoodManager RangeHoodManager::sRangeHoodMgr;

CHIP_ERROR RangeHoodManager::Init()
{
    // Endpoint initializations
    VerifyOrReturnError(mExtractorHoodEndpoint1.Init() == CHIP_NO_ERROR, CHIP_ERROR_INTERNAL);

    VerifyOrReturnError(mLightEndpoint2.Init() == CHIP_NO_ERROR, CHIP_ERROR_INTERNAL);

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

    DeviceLayer::PlatformMgr().LockChipStack();
    // read current on/off value on light endpoint.
    OnOffServer::Instance().getOnOffValue(kLightEndpoint2, &currentLedState);

    DataModel::Nullable<Percent> percentSettingNullable = GetPercentSetting();
    uint8_t percentSettingCB                            = percentSettingNullable.IsNull() ? 0 : percentSettingNullable.Value();
    PercentSettingWriteCallback(percentSettingCB);

    // Register FanControl delegate (GetFanDelegate returns pointer)
    FanControl::SetDefaultDelegate(kExtractorHoodEndpoint1, mExtractorHoodEndpoint1.GetFanDelegate());

    DeviceLayer::PlatformMgr().UnlockChipStack();

    mState = currentLedState ? kState_OnCompleted : kState_OffCompleted;

    return CHIP_NO_ERROR;
}

void RangeHoodManager::SetCallbacks(Callback_fn_initiated aActionInitiated_CB, Callback_fn_completed aActionCompleted_CB)
{
    mActionInitiated_CB = aActionInitiated_CB;
    mActionCompleted_CB = aActionCompleted_CB;
}

bool RangeHoodManager::IsActionInProgress()
{
    return (mState == kState_OffInitiated || mState == kState_OnInitiated);
}

bool RangeHoodManager::IsLightOn()
{
    return (mState == kState_OnCompleted);
}

void RangeHoodManager::EnableAutoTurnOff(bool aOn)
{
    mAutoTurnOff = aOn;
}

void RangeHoodManager::SetAutoTurnOffDuration(uint32_t aDurationInSecs)
{
    mAutoTurnOffDuration = aDurationInSecs;
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
    int32_t actor            = AppEvent::kEventType_Timer;
    uint8_t value            = aEvent->RangeHoodEvent.Value;

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
    int32_t actor            = AppEvent::kEventType_Timer;
    uint8_t value            = aEvent->RangeHoodEvent.Value;

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

    sRangeHoodMgr.mOffEffectArmed = true;
    sRangeHoodMgr.StartTimer(offEffectDuration);
}

Status RangeHoodManager::ProcessExtractorStepCommand(chip::EndpointId endpointId, StepDirectionEnum aDirection, bool aWrap,
                                                     bool aLowestOff)
{
    ChipLogProgress(AppServer, "RangeHoodManager::ProcessExtractorStepCommand  ep=%u  aDirection %d, aWrap %d, aLowestOff %d",
                    endpointId, to_underlying(aDirection), aWrap, aLowestOff);

    VerifyOrReturnError(aDirection != StepDirectionEnum::kUnknownEnumValue, Status::InvalidCommand);

    // if aLowestOff is true, Step command can reduce the fan to 0%, else 1%.
    uint8_t percentMin = aLowestOff ? kaLowestOffTrue : kaLowestOffFalse;
    uint8_t percentMax = 100;  // Maximum percent value

    DataModel::Nullable<Percent> percentSettingNullable = GetPercentSetting();
    uint8_t curPercentSetting = percentSettingNullable.IsNull() ? percentMin : percentSettingNullable.Value();

    // increase or decrease the fan percent by step size
    switch (aDirection)
    {
    case StepDirectionEnum::kIncrease: {
        if (curPercentSetting >= percentMax)
        {
            curPercentSetting = aWrap ? percentMin : percentMax;
        }
        else
        {
            curPercentSetting = std::min(percentMax, static_cast<uint8_t>(curPercentSetting + kStepSizePercent));
        }
        break;
    }
    case StepDirectionEnum::kDecrease: {
        if (curPercentSetting <= percentMin)
        {
            curPercentSetting = aWrap ? percentMax : percentMin;
        }
        else
        {
            curPercentSetting = std::max(percentMin, static_cast<uint8_t>(curPercentSetting - kStepSizePercent));
        }
        break;
    }
    default: {
        break;
    }
    }

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
    
    if (data->isPercentCurrent)
    {
        chip::app::Clusters::FanControl::Attributes::PercentCurrent::Set(data->endPoint, data->percentCurrent);
    }
    else if (data->isFanMode)
    {
        chip::app::Clusters::FanControl::Attributes::FanMode::Set(data->endPoint, data->fanMode);
    }
    else if (data->isPercentSetting)
    {
        chip::app::Clusters::FanControl::Attributes::PercentSetting::Set(data->endPoint, data->percentSetting);
    }
    chip::Platform::Delete(data);
}

void RangeHoodManager::HandleFanControlAttributeChange(AttributeId attributeId, uint8_t type, uint16_t size, uint8_t * value)
{
    switch (attributeId)
    {
    case chip::app::Clusters::FanControl::Attributes::PercentSetting::Id: {
        PercentSettingWriteCallback(*value);
        break;
    }

    case chip::app::Clusters::FanControl::Attributes::FanMode::Id: {
        mFanMode = *reinterpret_cast<FanModeEnum *>(value);
        FanModeWriteCallback(mFanMode);
#if DISPLAY_ENABLED
        UpdateRangeHoodLCD();
#endif
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
    // Set Percent Settings directly
    switch (aNewFanMode)
    {
    case FanModeEnum::kOff: {
        if (percentCurrent != kFanModeOffPercent)
        {
            SetPercentSetting(kFanModeOffPercent);
        }
        break;
    }
    case FanModeEnum::kLow: {
        if (percentCurrent != kFanModeLowPercent)
        {
            SetPercentSetting(kFanModeLowPercent);
        }
        break;
    }
    case FanModeEnum::kMedium: {
        if (percentCurrent != kFanModeMediumPercent)
        {
            SetPercentSetting(kFanModeMediumPercent);
        }
        break;
    }
    case FanModeEnum::kOn:
    case FanModeEnum::kHigh: {
        if (percentCurrent != kFanModeHighPercent)
        {
            SetPercentSetting(kFanModeHighPercent);
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

FanModeEnum RangeHoodManager::GetFanMode()
{
    return mFanMode;
}

DataModel::Nullable<Percent> RangeHoodManager::GetPercentSetting()
{
    DataModel::Nullable<Percent> percentSetting;

    Status status = chip::app::Clusters::FanControl::Attributes::PercentSetting::Get(kExtractorHoodEndpoint1, percentSetting);
    if (status != Status::Success)
    {
        ChipLogError(NotSpecified, "RangeHoodManager::GetPercentSetting: failed to get PercentSetting attribute: %d",
                     to_underlying(status));
    }

    return percentSetting;
}

void RangeHoodManager::UpdateRangeHoodLCD()
{
    AppTask::GetAppTask().UpdateRangeHoodUI();
}
