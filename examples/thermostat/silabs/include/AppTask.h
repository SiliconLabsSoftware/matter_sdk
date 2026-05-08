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

#include "BaseApplication.h"

#include <app/ConcreteAttributePath.h>
#include <cstdint>
#include <lib/core/CHIPError.h>

struct AppEvent;

#define APP_ERROR_CREATE_TIMER_FAILED CHIP_APPLICATION_ERROR(0x04)

class AppTask : public BaseApplication
{
public:
    AppTask() = default;

    static AppTask & GetAppTask();

    /**
     * @brief AppTask task main loop function.
     *
     * @param pvParameter FreeRTOS task parameter
     */
    static void AppTaskMain(void * pvParameter);

    CHIP_ERROR StartAppTask();

    /**
     * @brief Request an update of the Thermostat LCD UI.
     */
    void UpdateThermoStatUI();

    /**
     * @brief Event handler when a button is pressed.
     *
     * @param button    APP_FUNCTION_BUTTON
     * @param btnAction SL_SIMPLE_BUTTON_PRESSED, SL_SIMPLE_BUTTON_RELEASED or SL_SIMPLE_BUTTON_DISABLED
     */
    static void ButtonEventHandler(uint8_t button, uint8_t btnAction);

    /**
     * @brief Periodic CMSIS timer callback. Posts a temperature-update event into the AppTask queue.
     */
    static void SensorTimerEventHandler(void * arg);

    /**
     * @brief AppTask-thread handler that reads the sensor (Si70xx or simulated) and pushes
     *        Thermostat::LocalTemperature.
     */
    static void TemperatureUpdateEventHandler(AppEvent * aEvent);

    /**
     * @brief Thermostat-cluster post-attribute-change callback. Routes mode and setpoint changes
     *        into the local temperature cache and forwards to UI / AWS hooks.
     */
    void DMPostAttributeChangeCallback(const chip::app::ConcreteAttributePath & attributePath, uint8_t type, uint16_t size,
                                       uint8_t * value);

protected:
    /** Override of `BaseApplication::AppInit()`. */
    CHIP_ERROR AppInit() override;

    /** Bring up the thermostat domain: cluster cache, sensor timer, sensor driver. */
    CHIP_ERROR InitThermostat();
};
