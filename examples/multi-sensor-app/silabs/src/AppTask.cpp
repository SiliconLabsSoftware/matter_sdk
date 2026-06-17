/*
 *
 *    Copyright (c) 2024 Project CHIP Authors
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
#include "SensorManager.h"
#include <app/server/Server.h>
#include <app/util/attribute-storage.h>
#include <assert.h>
#include <cmsis_os2.h>
#include <lib/support/CodeUtils.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/silabs/platformAbstraction/SilabsPlatform.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <setup_payload/SetupPayload.h>
#include <sl_cmsis_os2_common.h>

#if defined (DISPLAY_ENABLED) && DISPLAY_ENABLED
#include <SensorsUI.h>
#endif

using namespace chip;
using namespace chip::DeviceLayer::Silabs;

namespace {

LEDWidget sOccupancyLed;

enum class ButtonTypes : uint8_t
{
    kFunctionButton    = 0,
    kApplicationButton = 1,
};

#if defined(SL_CATALOG_SIMPLE_LED_LED1_PRESENT)
constexpr uint8_t kOccupancyLedId = 1;
#else
constexpr uint8_t kOccupancyLedId = 0;
#endif

} // namespace

AppTask AppTask::sAppTask;

CHIP_ERROR AppTask::AppInit()
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    ChipLogProgress(AppServer, "AppInit: ButtonEventHandler set");
    GetPlatform().SetButtonsCb(AppTask::ButtonEventHandler);
    ChipLogProgress(AppServer, "AppInit: ButtonEventHandler set");

    sOccupancyLed.Init(kOccupancyLedId);
    sOccupancyLed.Set(false);

    err = SensorManager::Init();
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "SensorManager::Init failed");
        appError(err);
    }

#ifdef DISPLAY_ENABLED
    ChipLogProgress(AppServer, "AppInit: Display enabled");
    mCurrentSensorUI = kSensorUIEnum::kOccupancySensor;

// Show QR Code if not provisioned
#ifdef QR_CODE_ENABLED
    if (!BaseApplication::GetProvisionStatus())
    {
        GetLCD().ShowQRCode(true);
        mCurrentSensorUI = kSensorUIEnum::kQrCode;
    }
#endif // QR_CODE_ENABLED
    ChipLogProgress(AppServer, "AppInit: UpdateSensorDisplay");
    UpdateSensorDisplay();
    ChipLogProgress(AppServer, "AppInit: UpdateSensorDisplay completed");
#endif // DISPLAY_ENABLED

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
        ChipLogError(AppServer, "AppTask Init Failed!");
        appError(err);
    }

#if !(defined(CHIP_CONFIG_ENABLE_ICD_SERVER) && CHIP_CONFIG_ENABLE_ICD_SERVER)
    sAppTask.StartStatusLEDTimer();
#endif

    ChipLogProgress(AppServer, "AppTask LOL started.");

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

#ifdef DISPLAY_ENABLED
void AppTask::UpdateDisplay()
{
    ChipLogProgress(AppServer, "UpdateDisplay: entering UpdateDisplay");
    CycleSensorUI();
    ChipLogProgress(AppServer, "UpdateDisplay: CycleSensorUI completed");
    UpdateSensorDisplay();
    ChipLogProgress(AppServer, "UpdateSensorDisplay: UpdateSensorDisplay completed");
    ChipLogProgress(AppServer, "UpdateDisplay: UpdateDisplay completed");
}

void AppTask::UpdateSensorDisplay(void)
{
    switch (mCurrentSensorUI)
    {
    case kSensorUIEnum::kOccupancySensor:
        ChipLogProgress(AppServer, "UpdateSensorDisplay: entering occupancy screen");
        GetLCD().SetCustomUI(nullptr);
        GetLCD().WriteDemoUI(SensorManager::IsOccupancyDetected());
        ChipLogProgress(AppServer, "UpdateSensorDisplay: occupancy screen updated");
        break;
    case kSensorUIEnum::kSensor:
        ChipLogProgress(AppServer, "UpdateSensorDisplay: entering sensor screen");
        GetLCD().SetCustomUI(SensorsUI::SensorUI);
        GetLCD().WriteDemoUI();
        ChipLogProgress(AppServer, "UpdateSensorDisplay: sensor screen updated");
        break;
    case kSensorUIEnum::kStatusScreen:
        ChipLogProgress(AppServer, "UpdateSensorDisplay: entering status screen");
        BaseApplication::UpdateLCDStatusScreen();
        GetLCD().WriteStatus();
        ChipLogProgress(AppServer, "UpdateSensorDisplay: status screen updated");
        break;
#ifdef QR_CODE_ENABLED
    case kSensorUIEnum::kQrCode:
        ChipLogProgress(AppServer, "UpdateSensorDisplay: entering QR code screen");
        GetLCD().ShowQRCode(true);
        ChipLogProgress(AppServer, "UpdateSensorDisplay: QR code screen updated");
        break;
#endif
    default:
        // Handle unknown sensor
        // This should never happen
        ChipLogError(AppServer, "UpdateSensorDisplay: unknown UI state %u", static_cast<unsigned>(mCurrentSensorUI));
        break;
    }
}

void AppTask::CycleSensorUI()
{
    mCurrentSensorUI =
        static_cast<kSensorUIEnum>((static_cast<uint8_t>(mCurrentSensorUI) + 1) % static_cast<uint8_t>(kSensorUIEnum::kCount));
}
#endif // DISPLAY_ENABLED

void AppTask::ButtonEventHandler(uint8_t button, uint8_t btnAction)
{
    AppEvent button_event           = {};
    button_event.Type               = AppEvent::kEventType_Button;
    button_event.ButtonEvent.Action = btnAction;

    ChipLogProgress(AppServer, "ButtonEventHandler: button=%u action=%u", button, btnAction);

    switch (ButtonTypes(button))
    {
    case ButtonTypes::kFunctionButton:
        button_event.Handler = BaseApplication::ButtonHandler;
        sAppTask.PostEvent(&button_event);
        break;
    case ButtonTypes::kApplicationButton:
        if (SilabsPlatform::ButtonAction(btnAction) == SilabsPlatform::ButtonAction::ButtonPressed)
        {
            ChipLogProgress(AppServer, "Application button pressed; posting ProcessButtonEvent");
            button_event.Handler = ProccessButtonEvent;
            sAppTask.PostEvent(&button_event);
        }
        break;

    default:
        // Should never happen
        return;
    }
}

void AppTask::ProccessButtonEvent(AppEvent * event)
{
    VerifyOrReturn(event != nullptr);
    VerifyOrReturn(event->Type == AppEvent::kEventType_Button);

    ChipLogProgress(AppServer, "ProccessButtonEvent: dispatching button event to SensorManager");
    SensorManager::ButtonActionTriggered(event);
}

void AppTask::SensorAttributeUpdateEvent(AppEvent * event)
{
    VerifyOrReturn(event != nullptr);
    VerifyOrReturn(event->Type == AppEvent::kEventType_SensorAttributeUpdate);

#ifdef DISPLAY_ENABLED
    ChipLogProgress(AppServer, "SensorAttributeUpdateEvent: refreshing LCD");
    sAppTask.UpdateSensorDisplay();
    ChipLogProgress(AppServer, "SensorAttributeUpdateEvent: LCD refresh complete");
#endif // DISPLAY_ENABLED
}

void AppTask::OccupancyAttributeUpdateEvent(AppEvent * event)
{
    VerifyOrReturn(event != nullptr);
    VerifyOrReturn(event->Type == AppEvent::kEventType_OccupancyAttributeUpdate);

    ChipLogProgress(AppServer, "OccupancyAttributeUpdateEvent: updating LED to %u",
                    static_cast<unsigned>(event->OccupancyEvent.occupancyDetected));
    sOccupancyLed.Set(event->OccupancyEvent.occupancyDetected);
    ChipLogProgress(AppServer, "OccupancyAttributeUpdateEvent: LED update complete");

#ifdef DISPLAY_ENABLED
    ChipLogProgress(AppServer, "OccupancyAttributeUpdateEvent: refreshing LCD after occupancy change");
    sAppTask.UpdateSensorDisplay();
    ChipLogProgress(AppServer, "OccupancyAttributeUpdateEvent: LCD refresh complete");
#endif // DISPLAY_ENABLED
}
