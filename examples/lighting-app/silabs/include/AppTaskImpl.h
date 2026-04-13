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
#include <lib/core/CHIPError.h>

#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
#include "RGBLEDWidget.h"
#endif

#include <app/clusters/on-off-server/on-off-server.h>

/**
 * @brief CRTP base for AppTask that allows overrides for most AppTask APIs (public and protected).
 *
 * `StartAppTask()` is not a CRTP hook; it lives only on `AppTask`. Override only the *Impl methods you need;
 * all have defaults that call through to AppTask.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class AppTaskImpl : public AppTask
{
public:
    /**
     * Public API: each method dispatches to Derived::*Impl() via CRTP.
     * All *Impl() methods have defaults that call through to AppTask; override in Derived to customize.
     * Static methods dispatch via GetAppTask().
     */
    using Action_t = AppTask::Action_t;

    CHIP_ERROR AppInit() override { CRTP_RETURN_AND_VERIFY(AppTaskImpl, Derived, AppInit); }

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

    CHIP_ERROR InitLight() { CRTP_RETURN_AND_VERIFY(AppTaskImpl, Derived, InitLight); }

    bool IsLightOn() const { CRTP_RETURN_CONST_AND_VERIFY_ARGS(AppTaskImpl, Derived, IsLightOn); }

    void EnableAutoTurnOff(bool aOn) { CRTP_VOID_AND_VERIFY(AppTaskImpl, Derived, EnableAutoTurnOff, aOn); }

    void SetAutoTurnOffDuration(uint32_t aDurationInSecs)
    {
        CRTP_VOID_AND_VERIFY(AppTaskImpl, Derived, SetAutoTurnOffDuration, aDurationInSecs);
    }

    bool IsActionInProgress() const { CRTP_RETURN_CONST_AND_VERIFY_ARGS(AppTaskImpl, Derived, IsActionInProgress); }

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

    void DmCallbackMatterPostAttributeChangeCallback(const chip::app::ConcreteAttributePath & attributePath, uint8_t type,
                                                     uint16_t size, uint8_t * value)
    {
        CRTP_THIS(Derived)->DmCallbackMatterPostAttributeChangeCallbackImpl(attributePath, type, size, value);
    }

    void OnLightActionInitiated(AppTask::Action_t aAction, int32_t aActor, uint8_t * aValue)
    {
        CRTP_THIS(Derived)->OnLightActionInitiatedImpl(aAction, aActor, aValue);
    }

    void OnLightActionCompleted(AppTask::Action_t aAction) { CRTP_THIS(Derived)->OnLightActionCompletedImpl(aAction); }

    void StartLightTimer(uint32_t aTimeoutMs) { CRTP_THIS(Derived)->StartLightTimerImpl(aTimeoutMs); }

    void CancelLightTimer() { CRTP_THIS(Derived)->CancelLightTimerImpl(); }

    static void AppTaskMain(void * pvParameter) { CRTP_STATIC_VOID_AND_VERIFY(AppTaskImpl, Derived, AppTaskMain, pvParameter); }

    static void LightTimerEventHandler(void * timerCbArg) { CRTP_APP_TASK(Derived).LightTimerEventHandlerImpl(timerCbArg); }

    static void LightActionEventHandler(AppEvent * aEvent) { CRTP_APP_TASK(Derived).LightActionEventHandlerImpl(aEvent); }

    /** Public so AppTask.cpp can pass this pointer to ScheduleWork; dispatches to UpdateClusterStateImpl. */
    static void UpdateClusterState(intptr_t context) { CRTP_APP_TASK(Derived).UpdateClusterStateImpl(context); }

#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
    /** Public so AppTask.cpp can assign this handler; dispatches to LightControlEventHandlerImpl. */
    static void LightControlEventHandler(AppEvent * aEvent) { CRTP_APP_TASK(Derived).LightControlEventHandlerImpl(aEvent); }
#endif

    /** Public so AppTask.cpp can assign these handlers in LightTimerEventHandler; dispatch to *Impl. */
    static void AutoTurnOffTimerEventHandler(AppEvent * aEvent) { CRTP_APP_TASK(Derived).AutoTurnOffTimerEventHandlerImpl(aEvent); }

    static void ActuatorMovementTimerEventHandler(AppEvent * aEvent)
    {
        CRTP_APP_TASK(Derived).ActuatorMovementTimerEventHandlerImpl(aEvent);
    }

    static void OffEffectTimerEventHandler(AppEvent * aEvent) { CRTP_APP_TASK(Derived).OffEffectTimerEventHandlerImpl(aEvent); }

    /* Button UI events use BaseApplication::ButtonHandler (see AppTask.cpp); not CRTP-wrapped here. */

private:
    friend Derived;

    /**
     * Default *Impl() implementations: call through to AppTask. Override in Derived for custom behavior.
     */
    CHIP_ERROR AppInitImpl() { return AppTask::AppInit(); }

    void PostLightActionRequestImpl(int32_t aActor, Action_t aAction) { AppTask::PostLightActionRequest(aActor, aAction); }

#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
    void PostLightControlActionRequestImpl(int32_t aActor, Action_t aAction, RGBLEDWidget::ColorData_t * aValue)
    {
        AppTask::PostLightControlActionRequest(aActor, aAction, aValue);
    }
#endif

    void ButtonEventHandlerImpl(uint8_t button, uint8_t btnAction) { AppTask::ButtonEventHandler(button, btnAction); }

    void AppTaskMainImpl(void * pvParameter) { AppTask::AppTaskMain(pvParameter); }

    CHIP_ERROR InitLightImpl() { return AppTask::InitLight(); }

    bool IsLightOnImpl() const { return AppTask::IsLightOn(); }

    void EnableAutoTurnOffImpl(bool aOn) { AppTask::EnableAutoTurnOff(aOn); }

    void SetAutoTurnOffDurationImpl(uint32_t aDurationInSecs) { AppTask::SetAutoTurnOffDuration(aDurationInSecs); }

    bool IsActionInProgressImpl() const { return AppTask::IsActionInProgress(); }

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

    void OnTriggerOffWithEffectImpl(OnOffEffect * effect) { AppTask::OnTriggerOffWithEffect(effect); }

    void OnLightActionInitiatedImpl(AppTask::Action_t aAction, int32_t aActor, uint8_t * aValue)
    {
        AppTask::OnLightActionInitiated(aAction, aActor, aValue);
    }

    void OnLightActionCompletedImpl(AppTask::Action_t aAction) { AppTask::OnLightActionCompleted(aAction); }

    void StartLightTimerImpl(uint32_t aTimeoutMs) { AppTask::StartLightTimer(aTimeoutMs); }

    void CancelLightTimerImpl() { AppTask::CancelLightTimer(); }

    void LightTimerEventHandlerImpl(void * timerCbArg) { AppTask::LightTimerEventHandler(timerCbArg); }

    void AutoTurnOffTimerEventHandlerImpl(AppEvent * aEvent) { AppTask::AutoTurnOffTimerEventHandler(aEvent); }

    void ActuatorMovementTimerEventHandlerImpl(AppEvent * aEvent) { AppTask::ActuatorMovementTimerEventHandler(aEvent); }

    void OffEffectTimerEventHandlerImpl(AppEvent * aEvent) { AppTask::OffEffectTimerEventHandler(aEvent); }

    void LightActionEventHandlerImpl(AppEvent * aEvent) { AppTask::LightActionEventHandler(aEvent); }

#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
    void LightControlEventHandlerImpl(AppEvent * aEvent) { AppTask::LightControlEventHandler(aEvent); }
#endif

    void UpdateClusterStateImpl(intptr_t context) { AppTask::UpdateClusterState(context); }

    void DmCallbackMatterPostAttributeChangeCallbackImpl(const chip::app::ConcreteAttributePath & attributePath, uint8_t type,
                                                         uint16_t size, uint8_t * value)
    {
        AppTask::DmCallbackMatterPostAttributeChangeCallback(attributePath, type, size, value);
    }
};
