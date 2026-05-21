/*
 *
 *    Copyright (c) 2025 Project CHIP Authors
 *    Copyright (c) 2025 Google LLC.
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

/**
 * @brief CRTP override layer for closure-app `AppTask`.
 *
 * Each public entry point dispatches to a corresponding `Derived::*Impl()` hook.
 * Default `*Impl()` implementations forward to `AppTask`. Override in
 * `CustomerAppTask` to customize (see Silabs README).
 *
 * @tparam Derived The CRTP derived class (`CustomerAppTask`).
 */
template <typename Derived>
class AppTaskImpl : public AppTask
{
public:
    CHIP_ERROR AppInit() override { CRTP_OPTIONAL_DISPATCH(AppTaskImpl, Derived, AppInitImpl); }

    static void ButtonEventHandler(uint8_t button, uint8_t btnAction)
    {
        CRTP_OPTIONAL_STATIC_DISPATCH(AppTaskImpl, Derived, ButtonEventHandlerImpl, button, btnAction);
    }

    static void ClosureButtonActionEventHandler(AppEvent * aEvent)
    {
        CRTP_OPTIONAL_STATIC_DISPATCH(AppTaskImpl, Derived, ClosureButtonActionEventHandlerImpl, aEvent);
    }

#ifdef DISPLAY_ENABLED
    static void UpdateClosureUIHandler(AppEvent * aEvent)
    {
        CRTP_OPTIONAL_STATIC_DISPATCH(AppTaskImpl, Derived, UpdateClosureUIHandlerImpl, aEvent);
    }

    static void UpdateClosureUI()
    {
        CRTP_OPTIONAL_STATIC_DISPATCH(AppTaskImpl, Derived, UpdateClosureUIImpl);
    }
#endif // DISPLAY_ENABLED

    void DMPostAttributeChangeCallback(const chip::app::ConcreteAttributePath & attributePath, uint8_t type, uint16_t size,
                                       uint8_t * value)
    {
        CRTP_OPTIONAL_VOID_DISPATCH(AppTaskImpl, Derived, DMPostAttributeChangeCallbackImpl, attributePath, type, size, value);
    }

    void DMClosureControlClusterAttributeChangedCallback(const chip::app::ConcreteAttributePath & attributePath)
    {
        CRTP_OPTIONAL_VOID_DISPATCH(AppTaskImpl, Derived, DMClosureControlClusterAttributeChangedCallbackImpl, attributePath);
    }

    void DMClosureDimensionClusterAttributeChangedCallback(const chip::app::ConcreteAttributePath & attributePath)
    {
        CRTP_OPTIONAL_VOID_DISPATCH(AppTaskImpl, Derived, DMClosureDimensionClusterAttributeChangedCallbackImpl, attributePath);
    }

private:
    friend Derived;

    CHIP_ERROR AppInitImpl() { return AppTask::AppInit(); }

    void ButtonEventHandlerImpl(uint8_t button, uint8_t btnAction) { AppTask::ButtonEventHandler(button, btnAction); }

    void ClosureButtonActionEventHandlerImpl(AppEvent * aEvent) { AppTask::ClosureButtonActionEventHandler(aEvent); }

#ifdef DISPLAY_ENABLED
    void UpdateClosureUIHandlerImpl(AppEvent * aEvent) { AppTask::UpdateClosureUIHandler(aEvent); }

    void UpdateClosureUIImpl() { AppTask::UpdateClosureUI(); }
#endif // DISPLAY_ENABLED

    void DMPostAttributeChangeCallbackImpl(const chip::app::ConcreteAttributePath & attributePath, uint8_t type, uint16_t size,
                                           uint8_t * value)
    {
        AppTask::DMPostAttributeChangeCallback(attributePath, type, size, value);
    }

    void DMClosureControlClusterAttributeChangedCallbackImpl(const chip::app::ConcreteAttributePath & attributePath)
    {
        AppTask::DMClosureControlClusterAttributeChangedCallback(attributePath);
    }

    void DMClosureDimensionClusterAttributeChangedCallbackImpl(const chip::app::ConcreteAttributePath & attributePath)
    {
        AppTask::DMClosureDimensionClusterAttributeChangedCallback(attributePath);
    }
};
