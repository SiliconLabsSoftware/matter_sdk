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

/**********************************************************
 * Includes
 *********************************************************/

#include "AppEvent.h"
#include "BaseApplication.h"

#include <lib/core/CHIPError.h>

#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
#include "RGBLEDWidget.h"
#endif

#include <app/ConcreteAttributePath.h>
#include <app/clusters/on-off-server/on-off-server.h>

namespace chip {
namespace app {
class DeferredAttributePersistenceProvider;
} // namespace app
} // namespace chip

/**********************************************************
 * AppTask Declaration
 *
 * Lighting example application: platform entry, lighting state machine, and
 * data-model integration. Override hooks for the must-override APIs are
 * exposed through AppTaskImpl.h (CRTP).
 *
 * Lighting state (timers, level/color attribute caches)
 * lives as TU-local statics in AppTask.cpp, alongside the LED/effect objects.
 * AppTask is a singleton (CustomerAppTask::sAppTask), so the one-instance
 * constraint is the same as the prior design.
 *********************************************************/

class AppTask : public BaseApplication
{

public:
    AppTask() = default;

    static AppTask & GetAppTask();

    /**
     * @brief AppTask task main loop function
     *
     * @param pvParameter FreeRTOS task parameter
     */
    static void AppTaskMain(void * pvParameter);

    CHIP_ERROR StartAppTask();

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
     * @brief Event handler when a button is pressed.
     *
     * @param button      APP_LIGHT_SWITCH or APP_FUNCTION_BUTTON
     * @param btnAction   SL_SIMPLE_BUTTON_PRESSED, SL_SIMPLE_BUTTON_RELEASED or SL_SIMPLE_BUTTON_DISABLED
     */
    static void ButtonEventHandler(uint8_t button, uint8_t btnAction);

    CHIP_ERROR InitLight();

    /**
     * @brief Returns true when the light is currently in the On-completed state.
     */
    bool IsLightOn() const;

    bool InitiateAction(int32_t aActor, Action_t aAction, uint8_t * aValue);

#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
    bool InitiateLightCtrlAction(int32_t aActor, Action_t aAction, uint32_t aAttributeId, uint8_t * value);
#endif

    static void OnTriggerOffWithEffect(OnOffEffect * effect);

    void DMPostAttributeChangeCallback(const chip::app::ConcreteAttributePath & attributePath, uint8_t type, uint16_t size,
                                       uint8_t * value);

protected:
    CHIP_ERROR AppInit() override;

    void OnLightActionInitiated(Action_t aAction, int32_t aActor, uint8_t * aValue);
    void OnLightActionCompleted(Action_t aAction);

    static void LightTimerEventHandler(void * timerCbArg);

#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
    static void LightControlEventHandler(AppEvent * aEvent);
#endif

    chip::app::DeferredAttributePersistenceProvider * pDeferredAttributePersister = nullptr;
};
