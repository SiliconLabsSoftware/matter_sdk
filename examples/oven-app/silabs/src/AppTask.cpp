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

#include "AppTask.h"
#include "AppConfig.h"
#include "AppEvent.h"
#include "LEDWidget.h"
#include "OvenBindingHandler.h"
#include "OvenUI.h"

#ifdef DISPLAY_ENABLED
#include "lcd.h"
#include "OvenUI.h"
#ifdef QR_CODE_ENABLED
#include "qrcodegen.h"
#endif // QR_CODE_ENABLED
#endif // DISPLAY_ENABLED

#include <OvenManager.h>
#include <app-common/zap-generated/cluster-enums.h>
#include <app-common/zap-generated/cluster-objects.h>
#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/ConcreteAttributePath.h>
#include <app/clusters/network-commissioning/network-commissioning.h>
#include <app/clusters/on-off-server/on-off-server.h>
#include <app/server/Server.h>
#include <app/util/attribute-storage.h>
#include <app/util/endpoint-config-api.h>
#include <assert.h>
#include <lib/support/BitMask.h>
#include <lib/support/CodeUtils.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/silabs/platformAbstraction/SilabsPlatform.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <setup_payload/SetupPayload.h>

#ifdef SL_CATALOG_SIMPLE_LED_LED1_PRESENT
#define LIGHT_LED 1
#else
#define LIGHT_LED 0
#endif

#define APP_FUNCTION_BUTTON 0
#define APP_ACTION_BUTTON 1

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace ::chip::DeviceLayer;
using namespace ::chip::DeviceLayer::Silabs;
using namespace ::chip::DeviceLayer::Internal;
using namespace chip::TLV;

LEDWidget sLightLED; // Use LEDWidget for basic LED functionality

AppTask AppTask::sAppTask;

CHIP_ERROR AppTask::AppInit()
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    chip::DeviceLayer::Silabs::GetPlatform().SetButtonsCb(AppTask::ButtonEventHandler);

#ifdef DISPLAY_ENABLED
    GetLCD().Init((uint8_t *) "Oven-App");
    GetLCD().SetCustomUI(OvenUI::DrawUI);
#endif

    // Initialization of Oven Manager and endpoints of oven.
    OvenManager::GetInstance().Init();

    sLightLED.Init(LIGHT_LED);
    UpdateLED(OvenManager::GetInstance().GetCookTopState() == OvenManager::kCookTopState_On);

// Update the LCD with the Stored value. Show QR Code if not provisioned
#ifdef DISPLAY_ENABLED
    UpdateLCD();
#ifdef QR_CODE_ENABLED
    if (!BaseApplication::GetProvisionStatus())
    {
        GetLCD().ShowQRCode(true);
    }
#endif // QR_CODE_ENABLED
#endif

    return err;
}

CHIP_ERROR AppTask::StartAppTask()
{
    return BaseApplication::StartAppTask(AppTaskMain);
}

void AppTask::AppTaskMain(void * pvParameter)
{
    AppEvent event;
    osMessageQueueId_t sAppEventQueue = *(static_cast<osMessageQueueId_t *>(pvParameter));

    CHIP_ERROR err = sAppTask.Init();
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "AppTask.Init() failed");
        appError(err);
    }

#if !(defined(CHIP_CONFIG_ENABLE_ICD_SERVER) && CHIP_CONFIG_ENABLE_ICD_SERVER)
    sAppTask.StartStatusLEDTimer();
#endif

    ChipLogProgress(AppServer, "App Task started");

    while (true)
    {
        osStatus_t eventReceived = osMessageQueueGet(sAppEventQueue, &event, NULL, osWaitForever);
        while (eventReceived == osOK)
        {
            sAppTask.DispatchEvent(&event);
            eventReceived = osMessageQueueGet(sAppEventQueue, &event, NULL, 0);
        }
    }
}

void AppTask::ButtonEventHandler(uint8_t button, uint8_t btnAction)
{
    AppEvent button_event           = {};
    button_event.Type               = AppEvent::kEventType_Button;
    button_event.ButtonEvent.Action = btnAction;

    // Handle button1 specifically for oven functionality
    if (button == APP_ACTION_BUTTON)
    {
        button_event.Handler = OvenButtonHandler;
    }
    else
    {
        button_event.Handler = BaseApplication::ButtonHandler;
    }

    AppTask::GetAppTask().PostEvent(&button_event);
}

void AppTask::OvenButtonHandler(AppEvent * aEvent)
{
    // Handle button press to toggle cooktop and cook surface
    if (aEvent->ButtonEvent.Action == static_cast<uint8_t>(SilabsPlatform::ButtonAction::ButtonPressed))
    {
        ChipLogProgress(AppServer, "Oven button pressed - starting toggle action");
        return; // Only handle button release
    }

    if (aEvent->ButtonEvent.Action == static_cast<uint8_t>(SilabsPlatform::ButtonAction::ButtonReleased))
    {
        ChipLogProgress(AppServer, "Oven button released - toggling cooktop and cook surface");

        // Determine new state (toggle current state)
        OvenManager::Action_t action = (OvenManager::GetInstance().GetCookTopState() == OvenManager::kCookTopState_On) 
                                     ? OvenManager::COOK_TOP_OFF_ACTION 
                                     : OvenManager::COOK_TOP_ON_ACTION;
        
        // Toggle CookTop
        chip::DeviceLayer::PlatformMgr().ScheduleWork(UpdateClusterState, reinterpret_cast<intptr_t>(nullptr));

        // Trigger binding for cooktop endpoint (this will send commands to bound Matter devices)
        OnOffBindingContext * context = Platform::New<OnOffBindingContext>();
        if (context != nullptr)
        {
            context->localEndpointId = OvenManager::GetCookTopEndpoint(); // CookTop endpoint
            context->commandId = (action == OvenManager::COOK_TOP_ON_ACTION) ? OnOff::Commands::On::Id : OnOff::Commands::Off::Id;

            ChipLogProgress(AppServer, "Triggering binding for cooktop endpoint with command %lu", static_cast<unsigned long>(context->commandId));
            CookTopOnOffBindingTrigger(context);
        }
    }
}

void AppTask::UpdateClusterState(intptr_t context)
{
    // Update the OnOff cluster state for the cooktop endpoint based on the current state
    bool currentState = (OvenManager::GetInstance().GetCookTopState() == OvenManager::kCookTopState_On);

    ChipLogProgress(AppServer, "Updating cooktop OnOff cluster state to %s", currentState ? "On" : "Off");

    // Set the OnOff attribute value for the cooktop endpoint
    Protocols::InteractionModel::Status status = OnOffServer::Instance().setOnOffValue(OvenManager::GetCookTopEndpoint(), currentState, false);

    if (status != Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(AppServer, "Failed to update cooktop OnOff cluster state: %x", to_underlying(status));
    }
}

void AppTask::UpdateLED(int8_t value)
{
    sLightLED.Set(value);
}

void AppTask::UpdateLCD()
{
    // Update the LCD with the Stored value.
#ifdef DISPLAY_ENABLED
    GetLCD().WriteDemoUI(false);
#endif // DISPLAY_ENABLED
}
