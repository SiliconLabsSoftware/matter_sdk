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

#include <app/ConcreteAttributePath.h>
#include <app/clusters/on-off-server/on-off-server.h>
#include <app/persistence/DeferredAttributePersistenceProvider.h>
#include <ble/Ble.h>
#include <cmsis_os2.h>
#include <platform/CHIPDeviceLayer.h>

/**
 * @brief Lighting example application state and behavior (merged former LightingManager logic).
 */
class LightingAppTask : public AppTask
{

public:
    LightingAppTask() = default;

    enum Action_t
    {
        ON_ACTION = 0,
        OFF_ACTION,
        LEVEL_ACTION,
        COLOR_ACTION_HSV,
        COLOR_ACTION_CT,
        COLOR_ACTION_XY,

        INVALID_ACTION
    };

    enum State_t
    {
        kState_OffInitiated = 0,
        kState_OffCompleted,
        kState_OnInitiated,
        kState_OnCompleted,
    };

    /**
     * @brief Event handler when a button is pressed
     * Function posts an event for button processing
     *
     * @param buttonHandle APP_LIGHT_SWITCH or APP_FUNCTION_BUTTON
     * @param btnAction button action - SL_SIMPLE_BUTTON_PRESSED,
     *                  SL_SIMPLE_BUTTON_RELEASED or SL_SIMPLE_BUTTON_DISABLED
     */
    static void ButtonEventHandler(uint8_t button, uint8_t btnAction);
    void PostLightActionRequest(int32_t aActor, Action_t aAction);
#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
    void PostLightControlActionRequest(int32_t aActor, Action_t aAction, RGBLEDWidget::ColorData_t * aValue);
#endif // (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED)

    CHIP_ERROR InitLight();
    bool IsLightOn() const;
    void EnableAutoTurnOff(bool aOn);
    void SetAutoTurnOffDuration(uint32_t aDurationInSecs);
    bool IsActionInProgress() const;
    bool InitiateAction(int32_t aActor, Action_t aAction, uint8_t * aValue);
#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
    bool InitiateLightCtrlAction(int32_t aActor, Action_t aAction, uint32_t aAttributeId, uint8_t * value);
#endif
    static void OnTriggerOffWithEffect(OnOffEffect * effect);

    void DMPostAttributeChangeCallback(const chip::app::ConcreteAttributePath & attributePath, uint8_t type, uint16_t size,
                                       uint8_t * value);

protected:
    void OnLightActionInitiated(Action_t aAction, int32_t aActor, uint8_t * aValue);
    void OnLightActionCompleted(Action_t aAction);
    void StartLightTimer(uint32_t aTimeoutMs);
    void CancelLightTimer();
    static void LightTimerEventHandler(void * timerCbArg);
    static void AutoTurnOffTimerEventHandler(AppEvent * aEvent);
    static void ActuatorMovementTimerEventHandler(AppEvent * aEvent);
    static void OffEffectTimerEventHandler(AppEvent * aEvent);

    static void LightActionEventHandler(AppEvent * aEvent);
#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
    static void LightControlEventHandler(AppEvent * aEvent);
#endif // (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED)

    static void UpdateClusterState(intptr_t context);

    CHIP_ERROR AppInit() override;

private:
    State_t mLightState;
    uint8_t mCurrentLevel = 254;
#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
    uint8_t mCurrentHue        = 0;
    uint8_t mCurrentSaturation = 0;
    uint16_t mCurrentX         = 0;
    uint16_t mCurrentY         = 0;
    uint16_t mCurrentCTMireds  = 250;
#endif
    bool mAutoTurnOff             = false;
    uint32_t mAutoTurnOffDuration = 0;
    bool mAutoTurnOffTimerArmed   = false;
    bool mOffEffectArmed          = false;
    osTimerId_t mLightTimer       = nullptr;
};

/**
 * @brief CRTP base for AppTask that allows overrides for most LightingAppTask APIs (public and protected).
 *
 * `StartAppTask()` is not a CRTP hook; it lives only on `AppTask`. Override only the *Impl methods you need;
 * all have defaults that call through to LightingAppTask; override in Derived to customize.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class AppTaskImpl : public LightingAppTask
{
public:
    /**
     * Public API: each method dispatches to Derived::*Impl() via CRTP.
     * All *Impl() methods have defaults that call through to LightingAppTask; override in Derived to customize.
     * Static methods dispatch via GetAppTask().
     */
    using Action_t = LightingAppTask::Action_t;

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

    void DMPostAttributeChangeCallback(const chip::app::ConcreteAttributePath & attributePath, uint8_t type, uint16_t size,
                                       uint8_t * value)
    {
        CRTP_THIS(Derived)->DMPostAttributeChangeCallbackImpl(attributePath, type, size, value);
    }

    void OnLightActionInitiated(LightingAppTask::Action_t aAction, int32_t aActor, uint8_t * aValue)
    {
        CRTP_THIS(Derived)->OnLightActionInitiatedImpl(aAction, aActor, aValue);
    }

    void OnLightActionCompleted(LightingAppTask::Action_t aAction) { CRTP_THIS(Derived)->OnLightActionCompletedImpl(aAction); }

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
     * Default *Impl() implementations: call through to LightingAppTask. Override in Derived for custom behavior.
     */
    CHIP_ERROR AppInitImpl() { return LightingAppTask::AppInit(); }

    void PostLightActionRequestImpl(int32_t aActor, Action_t aAction) { LightingAppTask::PostLightActionRequest(aActor, aAction); }

#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
    void PostLightControlActionRequestImpl(int32_t aActor, Action_t aAction, RGBLEDWidget::ColorData_t * aValue)
    {
        LightingAppTask::PostLightControlActionRequest(aActor, aAction, aValue);
    }
#endif

    void ButtonEventHandlerImpl(uint8_t button, uint8_t btnAction) { LightingAppTask::ButtonEventHandler(button, btnAction); }

    void AppTaskMainImpl(void * pvParameter) { AppTask::AppTaskMain(pvParameter); }

    CHIP_ERROR InitLightImpl() { return LightingAppTask::InitLight(); }

    bool IsLightOnImpl() const { return LightingAppTask::IsLightOn(); }

    void EnableAutoTurnOffImpl(bool aOn) { LightingAppTask::EnableAutoTurnOff(aOn); }

    void SetAutoTurnOffDurationImpl(uint32_t aDurationInSecs) { LightingAppTask::SetAutoTurnOffDuration(aDurationInSecs); }

    bool IsActionInProgressImpl() const { return LightingAppTask::IsActionInProgress(); }

    bool InitiateActionImpl(int32_t aActor, Action_t aAction, uint8_t * aValue)
    {
        return LightingAppTask::InitiateAction(aActor, aAction, aValue);
    }

#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
    bool InitiateLightCtrlActionImpl(int32_t aActor, Action_t aAction, uint32_t aAttributeId, uint8_t * value)
    {
        return LightingAppTask::InitiateLightCtrlAction(aActor, aAction, aAttributeId, value);
    }
#endif

    void OnTriggerOffWithEffectImpl(OnOffEffect * effect) { LightingAppTask::OnTriggerOffWithEffect(effect); }

    void OnLightActionInitiatedImpl(LightingAppTask::Action_t aAction, int32_t aActor, uint8_t * aValue)
    {
        LightingAppTask::OnLightActionInitiated(aAction, aActor, aValue);
    }

    void OnLightActionCompletedImpl(LightingAppTask::Action_t aAction) { LightingAppTask::OnLightActionCompleted(aAction); }

    void StartLightTimerImpl(uint32_t aTimeoutMs) { LightingAppTask::StartLightTimer(aTimeoutMs); }

    void CancelLightTimerImpl() { LightingAppTask::CancelLightTimer(); }

    void LightTimerEventHandlerImpl(void * timerCbArg) { LightingAppTask::LightTimerEventHandler(timerCbArg); }

    void AutoTurnOffTimerEventHandlerImpl(AppEvent * aEvent) { LightingAppTask::AutoTurnOffTimerEventHandler(aEvent); }

    void ActuatorMovementTimerEventHandlerImpl(AppEvent * aEvent) { LightingAppTask::ActuatorMovementTimerEventHandler(aEvent); }

    void OffEffectTimerEventHandlerImpl(AppEvent * aEvent) { LightingAppTask::OffEffectTimerEventHandler(aEvent); }

    void LightActionEventHandlerImpl(AppEvent * aEvent) { LightingAppTask::LightActionEventHandler(aEvent); }

#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
    void LightControlEventHandlerImpl(AppEvent * aEvent) { LightingAppTask::LightControlEventHandler(aEvent); }
#endif

    void UpdateClusterStateImpl(intptr_t context) { LightingAppTask::UpdateClusterState(context); }

    void DMPostAttributeChangeCallbackImpl(const chip::app::ConcreteAttributePath & attributePath, uint8_t type, uint16_t size,
                                           uint8_t * value)
    {
        LightingAppTask::DMPostAttributeChangeCallback(attributePath, type, size, value);
    }
};
