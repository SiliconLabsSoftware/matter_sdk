/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *    Copyright (c) 2019 Google LLC.
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

#include "AppTask.h"
#include "CRTPHelpers.h"
#include "LightTypes.h"
#include <lib/core/CHIPError.h>

#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
#include "RGBLEDWidget.h"
#endif

#include <app/clusters/on-off-server/on-off-server.h>

/**
 * @brief CRTP base for AppTask that allows overrides for all AppTask APIs (public and protected).
 *
 * Override only the *Impl methods you need; others fall back to AppTask. Derived MUST implement
 * AppInitImpl(). All other Impl methods have defaults that call through to AppTask.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class AppTaskImpl : public AppTask
{
public:
    /**
     * Public API: each method dispatches to Derived::*Impl() via CRTP.
     * Derived must implement AppInitImpl(); all others may override *Impl() to customize.
     * Static methods dispatch via GetAppTask().
     */
    using Action_t = LightingManager::Action_t;

    CHIP_ERROR AppInit() override
    {
        CRTP_RETURN_DERIVED_ONLY(AppTaskImpl, Derived, AppInit);
    }

    CHIP_ERROR StartAppTask()
    {
        CRTP_RETURN_AND_VERIFY(AppTaskImpl, Derived, StartAppTask);
    }

    void PostLightActionRequest(int32_t aActor, Action_t aAction)
    {
        CRTP_VOID_AND_VERIFY(AppTaskImpl, Derived, PostLightActionRequest, aActor, aAction);
    }

#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
    void PostLightControlActionRequest(int32_t aActor, Action_t aAction, RGBLEDWidget::ColorData_t * aValue)
    {
        CRTP_VOID_AND_VERIFY(AppTaskImpl, Derived, PostLightControlActionRequest, aActor, aAction, aValue);
    }
#endif

    CHIP_ERROR InitLight()
    {
        CRTP_RETURN_AND_VERIFY(AppTaskImpl, Derived, InitLight);
    }

    bool IsLightOn() const
    {
        CRTP_RETURN_CONST_AND_VERIFY_ARGS(AppTaskImpl, Derived, IsLightOn);
    }

    void EnableAutoTurnOff(bool aOn)
    {
        CRTP_VOID_AND_VERIFY(AppTaskImpl, Derived, EnableAutoTurnOff, aOn);
    }

    void SetAutoTurnOffDuration(uint32_t aDurationInSecs)
    {
        CRTP_VOID_AND_VERIFY(AppTaskImpl, Derived, SetAutoTurnOffDuration, aDurationInSecs);
    }

    bool IsActionInProgress() const
    {
        CRTP_RETURN_CONST_AND_VERIFY_ARGS(AppTaskImpl, Derived, IsActionInProgress);
    }

    bool InitiateAction(int32_t aActor, Action_t aAction, uint8_t * aValue)
    {
        CRTP_RETURN_AND_VERIFY_ARGS(AppTaskImpl, Derived, InitiateAction, aActor, aAction, aValue);
    }

#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
    bool InitiateLightCtrlAction(int32_t aActor, Action_t aAction, uint32_t aAttributeId, uint8_t * value)
    {
        CRTP_RETURN_AND_VERIFY_ARGS(AppTaskImpl, Derived, InitiateLightCtrlAction, aActor, aAction, aAttributeId, value);
    }
#endif

    static void OnTriggerOffWithEffect(OnOffEffect * effect)
    {
        CRTP_STATIC_VOID_AND_VERIFY(AppTaskImpl, Derived, OnTriggerOffWithEffect, effect);
    }

    static void ButtonEventHandler(uint8_t button, uint8_t btnAction)
    {
        CRTP_STATIC_VOID_AND_VERIFY(AppTaskImpl, Derived, ButtonEventHandler, button, btnAction);
    }

    static void AppTaskMain(void * pvParameter)
    {
        CRTP_STATIC_VOID_AND_VERIFY(AppTaskImpl, Derived, AppTaskMain, pvParameter);
    }

protected:
    /**
     * AppTask overrides and static callbacks: forward to Derived::*Impl() via CRTP_THIS / CRTP_APP_TASK.
     * Override the corresponding *Impl() in Derived to customize.
     */
    void OnLightActionInitiated(LightingManager::Action_t aAction, int32_t aActor, uint8_t * aValue) override
    {
        CRTP_THIS(Derived)->OnLightActionInitiatedImpl(aAction, aActor, aValue);
    }

    void OnLightActionCompleted(LightingManager::Action_t aAction) override
    {
        CRTP_THIS(Derived)->OnLightActionCompletedImpl(aAction);
    }

    void StartLightTimer(uint32_t aTimeoutMs) override
    {
        CRTP_THIS(Derived)->StartLightTimerImpl(aTimeoutMs);
    }

    void CancelLightTimer() override
    {
        CRTP_THIS(Derived)->CancelLightTimerImpl();
    }

    static void LightTimerEventHandler(void * timerCbArg)
    {
        CRTP_APP_TASK(Derived).LightTimerEventHandlerImpl(timerCbArg);
    }

    static void AutoTurnOffTimerEventHandler(AppEvent * aEvent)
    {
        CRTP_APP_TASK(Derived).AutoTurnOffTimerEventHandlerImpl(aEvent);
    }

    static void ActuatorMovementTimerEventHandler(AppEvent * aEvent)
    {
        CRTP_APP_TASK(Derived).ActuatorMovementTimerEventHandlerImpl(aEvent);
    }

    static void OffEffectTimerEventHandler(AppEvent * aEvent)
    {
        CRTP_APP_TASK(Derived).OffEffectTimerEventHandlerImpl(aEvent);
    }

    static void LightActionEventHandler(AppEvent * aEvent)
    {
        CRTP_APP_TASK(Derived).LightActionEventHandlerImpl(aEvent);
    }

#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
    static void LightControlEventHandler(AppEvent * aEvent)
    {
        CRTP_APP_TASK(Derived).LightControlEventHandlerImpl(aEvent);
    }
#endif

    static void UpdateClusterState(intptr_t context)
    {
        CRTP_APP_TASK(Derived).UpdateClusterStateImpl(context);
    }

    static void ButtonHandler(AppEvent * aEvent)
    {
        CRTP_APP_TASK(Derived).ButtonHandlerImpl(aEvent);
    }

    static void SwitchActionEventHandler(AppEvent * aEvent)
    {
        CRTP_APP_TASK(Derived).SwitchActionEventHandlerImpl(aEvent);
    }

private:
    friend Derived;

    /**
     * Default *Impl() implementations: call through to AppTask. Override in Derived for custom behavior.
     */
    CHIP_ERROR StartAppTaskImpl()
    {
        return AppTask::StartAppTask();
    }

    void PostLightActionRequestImpl(int32_t aActor, Action_t aAction)
    {
        AppTask::PostLightActionRequest(aActor, aAction);
    }

#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
    void PostLightControlActionRequestImpl(int32_t aActor, Action_t aAction, RGBLEDWidget::ColorData_t * aValue)
    {
        AppTask::PostLightControlActionRequest(aActor, aAction, aValue);
    }
#endif

    void ButtonEventHandlerImpl(uint8_t button, uint8_t btnAction)
    {
        AppTask::ButtonEventHandler(button, btnAction);
    }

    void AppTaskMainImpl(void * pvParameter)
    {
        AppTask::AppTaskMain(pvParameter);
    }

    CHIP_ERROR InitLightImpl()
    {
        return AppTask::InitLight();
    }

    bool IsLightOnImpl() const
    {
        return AppTask::IsLightOn();
    }

    void EnableAutoTurnOffImpl(bool aOn)
    {
        AppTask::EnableAutoTurnOff(aOn);
    }

    void SetAutoTurnOffDurationImpl(uint32_t aDurationInSecs)
    {
        AppTask::SetAutoTurnOffDuration(aDurationInSecs);
    }

    bool IsActionInProgressImpl() const
    {
        return AppTask::IsActionInProgress();
    }

    bool InitiateActionImpl(int32_t aActor, Action_t aAction, uint8_t * aValue)
    {
        return AppTask::InitiateAction(aActor, aAction, aValue);
    }

#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
    bool InitiateLightCtrlActionImpl(int32_t aActor, Action_t aAction, uint32_t aAttributeId, uint8_t * value)
    {
        return AppTask::InitiateLightCtrlAction(aActor, aAction, aAttributeId, value);
    }
#endif

    void OnTriggerOffWithEffectImpl(OnOffEffect * effect)
    {
        AppTask::OnTriggerOffWithEffect(effect);
    }

    void OnLightActionInitiatedImpl(LightingManager::Action_t aAction, int32_t aActor, uint8_t * aValue)
    {
        AppTask::OnLightActionInitiated(aAction, aActor, aValue);
    }

    void OnLightActionCompletedImpl(LightingManager::Action_t aAction)
    {
        AppTask::OnLightActionCompleted(aAction);
    }

    void StartLightTimerImpl(uint32_t aTimeoutMs)
    {
        AppTask::StartLightTimer(aTimeoutMs);
    }

    void CancelLightTimerImpl()
    {
        AppTask::CancelLightTimer();
    }

    void LightTimerEventHandlerImpl(void * timerCbArg)
    {
        AppTask::LightTimerEventHandler(timerCbArg);
    }

    void AutoTurnOffTimerEventHandlerImpl(AppEvent * aEvent)
    {
        AppTask::AutoTurnOffTimerEventHandler(aEvent);
    }

    void ActuatorMovementTimerEventHandlerImpl(AppEvent * aEvent)
    {
        AppTask::ActuatorMovementTimerEventHandler(aEvent);
    }

    void OffEffectTimerEventHandlerImpl(AppEvent * aEvent)
    {
        AppTask::OffEffectTimerEventHandler(aEvent);
    }

    void LightActionEventHandlerImpl(AppEvent * aEvent)
    {
        AppTask::LightActionEventHandler(aEvent);
    }

#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
    void LightControlEventHandlerImpl(AppEvent * aEvent)
    {
        AppTask::LightControlEventHandler(aEvent);
    }
#endif

    void UpdateClusterStateImpl(intptr_t context)
    {
        AppTask::UpdateClusterState(context);
    }

    void ButtonHandlerImpl(AppEvent * aEvent)
    {
        AppTask::ButtonHandler(aEvent);
    }

    void SwitchActionEventHandlerImpl(AppEvent * aEvent)
    {
        AppTask::SwitchActionEventHandler(aEvent);
    }
};
