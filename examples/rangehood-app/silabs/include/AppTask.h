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

#include "AppEvent.h"
#include "BaseApplication.h"
#include "ExtractorHoodEndpoint.h"
#include "LightEndpoint.h"

#include <app/ConcreteAttributePath.h>
#include <lib/core/CHIPError.h>

// Application-defined error codes in the CHIP_ERROR space.
#define APP_ERROR_EVENT_QUEUE_FAILED CHIP_APPLICATION_ERROR(0x01)
#define APP_ERROR_CREATE_TASK_FAILED CHIP_APPLICATION_ERROR(0x02)
#define APP_ERROR_UNHANDLED_EVENT CHIP_APPLICATION_ERROR(0x03)
#define APP_ERROR_CREATE_TIMER_FAILED CHIP_APPLICATION_ERROR(0x04)
#define APP_ERROR_START_TIMER_FAILED CHIP_APPLICATION_ERROR(0x05)
#define APP_ERROR_STOP_TIMER_FAILED CHIP_APPLICATION_ERROR(0x06)

class AppTask : public BaseApplication
{
public:
    enum Action_t : uint8_t
    {
        LIGHT_ON_ACTION = 0,
        LIGHT_OFF_ACTION,
        FAN_PERCENT_CHANGE_ACTION,
        FAN_MODE_CHANGE_ACTION,

        INVALID_ACTION
    };

    AppTask() = default;

    static AppTask & GetAppTask();

    static void AppTaskMain(void * pvParameter);

    CHIP_ERROR StartAppTask();

    static void ButtonEventHandler(uint8_t button, uint8_t btnAction);

    static void ActionTriggerHandler(AppEvent * aEvent);

    static void FanControlButtonHandler(AppEvent * aEvent);

    void DMPostAttributeChangeCallback(const chip::app::ConcreteAttributePath & attributePath, uint8_t type, uint16_t size,
                                       uint8_t * value);

    static ExtractorHoodEndpoint & GetExtractorHoodEndpoint();
    static LightEndpoint & GetLightEndpoint();

protected:
    CHIP_ERROR AppInit() override;
    CHIP_ERROR InitRangeHood();
};
