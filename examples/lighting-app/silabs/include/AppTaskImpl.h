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

/**
 * @brief CRTP base for AppTask, exposing override hooks for the must-override APIs only.
 *
 * Each public method dispatches to `Derived::*Impl()` via CRTP. Default `*Impl()` methods
 * call through to `AppTask::Method()` (the underlying default behavior). Override the
 * `*Impl()` methods in Derived to customize.
 *
 * Internal plumbing (state queries, timer wrappers, sub-handlers, event posters) lives
 * as TU-local statics in AppTask.cpp and is intentionally not exposed here.
 *
 * `StartAppTask()` is not a CRTP hook; it lives only on `AppTask`.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class AppTaskImpl : public AppTask
{
public:
    using Action_t = AppTask::Action_t;

    CHIP_ERROR AppInit() override { CRTP_RETURN_AND_VERIFY(AppTaskImpl, Derived, AppInit); }

    CHIP_ERROR InitLight() { CRTP_RETURN_AND_VERIFY(AppTaskImpl, Derived, InitLight); }

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

    void OnLightActionInitiated(Action_t aAction, int32_t aActor, uint8_t * aValue)
    {
        CRTP_THIS(Derived)->OnLightActionInitiatedImpl(aAction, aActor, aValue);
    }

    void OnLightActionCompleted(Action_t aAction) { CRTP_THIS(Derived)->OnLightActionCompletedImpl(aAction); }

    /** Public so AppTask.cpp can pass this pointer to osTimerNew; dispatches to LightTimerEventHandlerImpl. */
    static void LightTimerEventHandler(void * timerCbArg) { CRTP_APP_TASK(Derived).LightTimerEventHandlerImpl(timerCbArg); }

    /** Public so AppTask.cpp can pass this pointer to ScheduleWork; dispatches to UpdateClusterStateImpl. */
    static void UpdateClusterState(intptr_t context) { CRTP_APP_TASK(Derived).UpdateClusterStateImpl(context); }

#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
    /** Public so AppTask.cpp can assign this handler; dispatches to LightControlEventHandlerImpl. */
    static void LightControlEventHandler(AppEvent * aEvent) { CRTP_APP_TASK(Derived).LightControlEventHandlerImpl(aEvent); }
#endif

private:
    friend Derived;

    /**
     * Default *Impl() implementations: call through to AppTask. Override in Derived for custom behavior.
     */
    CHIP_ERROR AppInitImpl() { return AppTask::AppInit(); }

    CHIP_ERROR InitLightImpl() { return AppTask::InitLight(); }

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

    void ButtonEventHandlerImpl(uint8_t button, uint8_t btnAction) { AppTask::ButtonEventHandler(button, btnAction); }

    void DMPostAttributeChangeCallbackImpl(const chip::app::ConcreteAttributePath & attributePath, uint8_t type, uint16_t size,
                                           uint8_t * value)
    {
        AppTask::DMPostAttributeChangeCallback(attributePath, type, size, value);
    }

    void OnLightActionInitiatedImpl(Action_t aAction, int32_t aActor, uint8_t * aValue)
    {
        AppTask::OnLightActionInitiated(aAction, aActor, aValue);
    }

    void OnLightActionCompletedImpl(Action_t aAction) { AppTask::OnLightActionCompleted(aAction); }

    void LightTimerEventHandlerImpl(void * timerCbArg) { AppTask::LightTimerEventHandler(timerCbArg); }

    void UpdateClusterStateImpl(intptr_t context) { AppTask::UpdateClusterState(context); }

#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
    void LightControlEventHandlerImpl(AppEvent * aEvent) { AppTask::LightControlEventHandler(aEvent); }
#endif
};
