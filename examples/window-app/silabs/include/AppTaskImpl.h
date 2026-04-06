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

/**
 * @brief CRTP base for AppTask that allows overrides for all AppTask APIs (public and protected).
 *
 * Override only the *Impl methods you need; all have defaults that call through to AppTask.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class AppTaskImpl : public AppTask
{
public:
    // --- Public API: CRTP dispatch to Derived::*Impl() ---

    CHIP_ERROR AppInit() override { CRTP_RETURN_AND_VERIFY(AppTaskImpl, Derived, AppInit); }

    CHIP_ERROR StartAppTask() { CRTP_RETURN_AND_VERIFY(AppTaskImpl, Derived, StartAppTask); }

    void PostAttributeChange(chip::EndpointId endpoint, chip::AttributeId attributeId)
    {
        CRTP_VOID_AND_VERIFY(AppTaskImpl, Derived, PostAttributeChange, endpoint, attributeId);
    }

    void UpdateLED() { CRTP_VOID_AND_VERIFY(AppTaskImpl, Derived, UpdateLED); }

    static void ButtonEventHandler(uint8_t button, uint8_t btnAction)
    {
        CRTP_STATIC_VOID_AND_VERIFY(AppTaskImpl, Derived, ButtonEventHandler, button, btnAction);
    }

    static void AppTaskMain(void * pvParameter) { CRTP_STATIC_VOID_AND_VERIFY(AppTaskImpl, Derived, AppTaskMain, pvParameter); }

#ifdef DISPLAY_ENABLED
    void UpdateLCD() { CRTP_VOID_AND_VERIFY(AppTaskImpl, Derived, UpdateLCD); }

    static void DrawUI(GLIB_Context_t * glibContext)
    {
        CRTP_STATIC_VOID_AND_VERIFY(AppTaskImpl, Derived, DrawUI, glibContext);
    }
#endif

protected:
    // --- Virtual method overrides (vtable -> CRTP) ---

    void DmCallbackMatterPostAttributeChangeCallback(const chip::app::ConcreteAttributePath & attributePath, uint8_t type,
                                                     uint16_t size, uint8_t * value) override
    {
        CRTP_THIS(Derived)->DmCallbackMatterPostAttributeChangeCallbackImpl(attributePath, type, size, value);
    }

    void DmCallbackMatterWindowCoveringClusterServerAttributeChangedCallback(
        const chip::app::ConcreteAttributePath & attributePath) override
    {
        CRTP_THIS(Derived)->DmCallbackMatterWindowCoveringClusterServerAttributeChangedCallbackImpl(attributePath);
    }

    // --- Static handler wrappers ---

    static void GeneralEventHandler(AppEvent * aEvent) { CRTP_APP_TASK(Derived).GeneralEventHandlerImpl(aEvent); }

    static void OnIconTimeout(AppTask::Timer & timer) { CRTP_APP_TASK(Derived).OnIconTimeoutImpl(timer); }

    static void OnLongPressTimeout(AppTask::Timer & timer) { CRTP_APP_TASK(Derived).OnLongPressTimeoutImpl(timer); }

private:
    friend Derived;

    // --- Default *Impl() implementations: call through to AppTask ---

    CHIP_ERROR AppInitImpl() { return AppTask::AppInit(); }

    CHIP_ERROR StartAppTaskImpl() { return AppTask::StartAppTask(); }

    void PostAttributeChangeImpl(chip::EndpointId endpoint, chip::AttributeId attributeId)
    {
        AppTask::PostAttributeChange(endpoint, attributeId);
    }

    void UpdateLEDImpl() { AppTask::UpdateLED(); }

    void ButtonEventHandlerImpl(uint8_t button, uint8_t btnAction) { AppTask::ButtonEventHandler(button, btnAction); }

    void AppTaskMainImpl(void * pvParameter) { AppTask::AppTaskMain(pvParameter); }

#ifdef DISPLAY_ENABLED
    void UpdateLCDImpl() { AppTask::UpdateLCD(); }
    void DrawUIImpl(GLIB_Context_t * glibContext) { AppTask::DrawUI(glibContext); }
#endif

    void GeneralEventHandlerImpl(AppEvent * aEvent) { AppTask::GeneralEventHandler(aEvent); }

    void OnIconTimeoutImpl(AppTask::Timer & timer) { AppTask::OnIconTimeout(timer); }

    void OnLongPressTimeoutImpl(AppTask::Timer & timer) { AppTask::OnLongPressTimeout(timer); }

    void DmCallbackMatterPostAttributeChangeCallbackImpl(const chip::app::ConcreteAttributePath & attributePath, uint8_t type,
                                                         uint16_t size, uint8_t * value)
    {
        AppTask::DmCallbackMatterPostAttributeChangeCallback(attributePath, type, size, value);
    }

    void DmCallbackMatterWindowCoveringClusterServerAttributeChangedCallbackImpl(
        const chip::app::ConcreteAttributePath & attributePath)
    {
        AppTask::DmCallbackMatterWindowCoveringClusterServerAttributeChangedCallback(attributePath);
    }
};
