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

#include "CommonAppTask.h"
#include "AppTask.h"
#include "AppConfig.h"
#include "AppEvent.h"

#include <platform/CHIPDeviceLayer.h>
#include <platform/silabs/platformAbstraction/SilabsPlatform.h>
#include <platform/silabs/tracing/SilabsTracingMacros.h>

using namespace ::chip::DeviceLayer::Silabs;

#define APP_FUNCTION_BUTTON 0
#define APP_LIGHT_SWITCH 1

CommonAppTask CommonAppTask::sAppTask;

AppTask & AppTask::GetAppTask()
{
    return CommonAppTask::GetAppTask();
}

CHIP_ERROR CommonAppTask::AppInitImpl()
{
    SILABS_LOG("CommonAppTask: custom implementation (AppInitImpl)");
    CHIP_ERROR err = this->AppTask::AppInit();
    if (err == CHIP_NO_ERROR)
    {
        chip::DeviceLayer::Silabs::GetPlatform().SetButtonsCb(CommonAppTask::ButtonEventHandler);
    }
    return err;
}

void CommonAppTask::ButtonEventHandlerImpl(uint8_t button, uint8_t btnAction)
{
    SILABS_LOG("CommonAppTask: custom implementation (ButtonEventHandlerImpl)");
    AppEvent button_event           = {};
    button_event.Type               = AppEvent::kEventType_Button;
    button_event.ButtonEvent.Action = btnAction;

    if (button == APP_LIGHT_SWITCH && btnAction == static_cast<uint8_t>(SilabsPlatform::ButtonAction::ButtonPressed))
    {
        button_event.Handler = LightActionEventHandler;
        AppTask::GetAppTask().PostEvent(&button_event);
    }
    else if (button == APP_FUNCTION_BUTTON)
    {
        button_event.Handler = BaseApplication::ButtonHandler;
        AppTask::GetAppTask().PostEvent(&button_event);
    }
}
