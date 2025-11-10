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

#include "RangeHoodManager.h"

#include "ExtractorHoodEndpoint.h"
#include "LightEndpoint.h"

#include "AppConfig.h"
#include "AppTask.h"

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app/clusters/fan-control-server/fan-control-server.h>
#include <lib/support/TypeTraits.h>
#include <platform/CHIPDeviceLayer.h>

using namespace chip;
using namespace chip::app;
using namespace ::chip::app::Clusters;
using namespace ::chip::app::Clusters::FanControl;

using namespace ::chip::app::Clusters::OnOff;
using namespace chip::Protocols::InteractionModel;
using namespace ::chip::DeviceLayer;

RangeHoodManager RangeHoodManager::sRangeHoodMgr;

namespace {

/**********************************************************
 * OffWithEffect Callbacks
 *********************************************************/

OnOffEffect gEffect = {
    chip::EndpointId{ 2 }, // kLightEndpoint
    RangeHoodManager::OnTriggerOffWithEffect,
    EffectIdentifierEnum::kDelayedAllOff,
    to_underlying(DelayedAllOffEffectVariantEnum::kDelayedOffFastFade),
};

} // namespace

CHIP_ERROR RangeHoodManager::Init()
{   
    // Endpoint initializations with fan mode percent mappings
    VerifyOrReturnError(mExtractorHoodEndpoint.Init(
        0,    // Off: 0%
        30,   // Low: 30%
        60,   // Medium: 60%
        100   // High: 100%
    ) == CHIP_NO_ERROR, CHIP_ERROR_INTERNAL);

    VerifyOrReturnError(mLightEndpoint.Init() == CHIP_NO_ERROR, CHIP_ERROR_INTERNAL);

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

    return CHIP_NO_ERROR;
}

void RangeHoodManager::Shutdown()
{
    if (mLightTimer != nullptr)
    {
        CancelTimer();
        osTimerDelete(mLightTimer);
        mLightTimer = nullptr;
    }
}

bool RangeHoodManager::IsLightOn()
{
    return mLightEndpoint.IsLightOn();
}

bool RangeHoodManager::HandleLightAction(Action_t aAction)
{
    bool action_initiated = false;
    bool currentLightState = IsLightOn();

    // Initiate Turn On/Off Action only when it would change the state
    // Allow ON if light is off or if off effect is armed (can interrupt off effect)
    if ((!currentLightState || mOffEffectArmed) && aAction == LIGHT_ON_ACTION)
    {
        action_initiated = true;

        if (mOffEffectArmed)
        {
            CancelTimer();
            mOffEffectArmed = false;
        }
    }
    // Allow OFF if light is on and off effect is not armed
    else if (currentLightState && aAction == LIGHT_OFF_ACTION && !mOffEffectArmed)
    {
        action_initiated = true;

        if (mAutoTurnOffTimerArmed)
        {
            // If auto turn off timer has been armed and someone initiates turning off,
            // cancel the timer and continue as normal.
            mAutoTurnOffTimerArmed = false;
            CancelTimer();
        }
    }

    if (action_initiated && (aAction == LIGHT_ON_ACTION || aAction == LIGHT_OFF_ACTION))
    {
        AppEvent event;
        event.Type = AppEvent::kEventType_RangeHood;
        event.RangeHoodEvent.Action = aAction;
        event.Handler = AppTask::ActionTriggerHandler;
        AppTask::GetAppTask().PostEvent(&event);

        if (aAction == LIGHT_ON_ACTION && mLightEndpoint.IsAutoTurnOffEnabled())
        {
            uint32_t duration = mLightEndpoint.GetAutoTurnOffDuration();
            if (duration > 0)
            {
                StartTimer(duration * 1000);
                mAutoTurnOffTimerArmed = true;
                SILABS_LOG("Auto Turn off enabled. Will be triggered in %u seconds", duration);
            }
        }
    }

    return action_initiated;
}

void RangeHoodManager::StartTimer(uint32_t aTimeoutMs)
{
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

    if (light == nullptr)
    {
        ChipLogError(NotSpecified, "TimerEventHandler: null context, ignoring timer event");
        return;
    }

    // The timer event handler will be called in the context of the timer task
    // once mLightTimer expires. Post an event to apptask queue with the actual handler
    // so that the event can be handled in the context of the apptask.
    AppEvent event;
    event.Type               = AppEvent::kEventType_Timer;
    event.TimerEvent.Context = light;
    if (light->mAutoTurnOffTimerArmed)
    {
        event.Handler = AutoTurnOffTimerEventHandler;
        AppTask::GetAppTask().PostEvent(&event);
    }
    else if (light->mOffEffectArmed)
    {
        event.Handler = OffEffectTimerEventHandler;
        AppTask::GetAppTask().PostEvent(&event);
    }
}

void RangeHoodManager::AutoTurnOffTimerEventHandler(AppEvent * aEvent)
{
    RangeHoodManager * light = static_cast<RangeHoodManager *>(aEvent->TimerEvent.Context);

    if (!light->mAutoTurnOffTimerArmed)
    {
        return;
    }

    light->mAutoTurnOffTimerArmed = false;

    SILABS_LOG("Auto Turn Off has been triggered!");

    light->HandleLightAction(LIGHT_OFF_ACTION);
}

void RangeHoodManager::OffEffectTimerEventHandler(AppEvent * aEvent)
{
    RangeHoodManager * light = static_cast<RangeHoodManager *>(aEvent->TimerEvent.Context);

    if (!light->mOffEffectArmed)
    {
        return;
    }

    light->mOffEffectArmed = false;

    SILABS_LOG("OffEffect completed");

    light->HandleLightAction(LIGHT_OFF_ACTION);
}

void RangeHoodManager::OnTriggerOffWithEffect(OnOffEffect * effect)
{
    auto effectId              = effect->mEffectIdentifier;
    auto effectVariant         = effect->mEffectVariant;
    uint32_t offEffectDuration = 0;

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

void RangeHoodManager::FanControlAttributeChangeHandler(chip::EndpointId endpointId, chip::AttributeId attributeId, uint8_t * value, uint16_t size)
{
    if (endpointId != kExtractorHoodEndpoint)
    {
        ChipLogError(NotSpecified, "FanControlAttributeChangeHandler: Invalid endpoint %u, expected %u", endpointId, kExtractorHoodEndpoint);
        return;
    }

    if (value == nullptr)
    {
        ChipLogError(NotSpecified, "FanControlAttributeChangeHandler: Invalid value pointer");
        return;
    }

    Action_t action = INVALID_ACTION;
    
    switch (attributeId)
    {
    case chip::app::Clusters::FanControl::Attributes::PercentSetting::Id: {
        mExtractorHoodEndpoint.HandlePercentSettingChange(*value);
        action = FAN_PERCENT_CHANGE_ACTION;
        break;
    }

    case chip::app::Clusters::FanControl::Attributes::FanMode::Id: {
        mExtractorHoodEndpoint.HandleFanModeChange(*reinterpret_cast<FanModeEnum *>(value));
        action = FAN_MODE_CHANGE_ACTION;
        break;
    }

    default: {
        return;
    }
    }

    if (action != INVALID_ACTION)
    {
        AppEvent event;
        event.Type = AppEvent::kEventType_RangeHood;
        event.RangeHoodEvent.Action = action;
        event.Handler = AppTask::ActionTriggerHandler;
        AppTask::GetAppTask().PostEvent(&event);
    }
}

void RangeHoodManager::OnOffAttributeChangeHandler(chip::EndpointId endpointId, chip::AttributeId attributeId, uint8_t * value, uint16_t size)
{
    if (endpointId != kLightEndpoint)
    {
        ChipLogError(NotSpecified, "OnOffAttributeChangeHandler: Invalid endpoint %u, expected %u", endpointId, kLightEndpoint);
        return;
    }

    if (value == nullptr || size < sizeof(uint8_t))
    {
        ChipLogError(NotSpecified, "OnOffAttributeChangeHandler: Invalid value or size");
        return;
    }

    // Call HandleLightAction directly - it will handle posting event to AppTask if needed
    Action_t action = *value ? LIGHT_ON_ACTION : LIGHT_OFF_ACTION;
    HandleLightAction(action);
}
