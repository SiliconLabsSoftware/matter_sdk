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

#include <stdbool.h>
#include <stdint.h>
#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
#include "RGBLEDWidget.h"
#endif //(defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
#include "AppEvent.h"
#include "BaseApplication.h"

#include <app/ConcreteAttributePath.h>
#include <app/clusters/on-off-server/on-off-server.h>
#include <app/persistence/DeferredAttributePersistenceProvider.h>
#include <ble/Ble.h>
#include <cmsis_os2.h>
#include <lib/core/CHIPError.h>
#include <platform/CHIPDeviceLayer.h>

/**********************************************************
 * Defines
 *********************************************************/

// Application-defined error codes in the CHIP_ERROR space.
#define APP_ERROR_EVENT_QUEUE_FAILED CHIP_APPLICATION_ERROR(0x01)
#define APP_ERROR_CREATE_TASK_FAILED CHIP_APPLICATION_ERROR(0x02)
#define APP_ERROR_UNHANDLED_EVENT CHIP_APPLICATION_ERROR(0x03)
#define APP_ERROR_CREATE_TIMER_FAILED CHIP_APPLICATION_ERROR(0x04)
#define APP_ERROR_START_TIMER_FAILED CHIP_APPLICATION_ERROR(0x05)
#define APP_ERROR_STOP_TIMER_FAILED CHIP_APPLICATION_ERROR(0x06)

/**********************************************************
 * AppTask Declaration
 *********************************************************/

class AppTask : public BaseApplication
{

public:
    AppTask() = default;

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

    static AppTask & GetAppTask();

    /**
     * @brief AppTask task main loop function
     *
     * @param pvParameter FreeRTOS task parameter
     */
    static void AppTaskMain(void * pvParameter);

    CHIP_ERROR StartAppTask();

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

    virtual void DmCallbackMatterPostAttributeChangeCallback(const chip::app::ConcreteAttributePath & attributePath,
                                                             uint8_t type, uint16_t size, uint8_t * value);

protected:
    virtual void OnLightActionInitiated(Action_t aAction, int32_t aActor, uint8_t * aValue);
    virtual void OnLightActionCompleted(Action_t aAction);
    virtual void StartLightTimer(uint32_t aTimeoutMs);
    virtual void CancelLightTimer();
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

    static void ButtonHandler(AppEvent * aEvent);
    static void SwitchActionEventHandler(AppEvent * aEvent);

private:
    chip::app::DeferredAttributePersistenceProvider * pDeferredAttributePersister = nullptr;

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
