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

#include "AppTask.h"
#include "AppConfig.h"
#include "AppEvent.h"
#include "LEDWidget.h"

#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/clusters/on-off-server/on-off-server.h>
#include <app/server/Server.h>
#include <app/util/attribute-storage.h>

#include <assert.h>

#include <setup_payload/OnboardingCodesUtil.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <setup_payload/SetupPayload.h>

#include <lib/support/CodeUtils.h>
#include <lib/support/TypeTraits.h>

#include <platform/CHIPDeviceLayer.h>

#include <platform/silabs/platformAbstraction/SilabsPlatform.h>

#ifdef DIC_ENABLE
#include "dic.h"
#endif // DIC_ENABLE

#ifdef SL_CATALOG_SIMPLE_LED_LED1_PRESENT
#define ONOFF_LED 1
#else
#define ONOFF_LED 0
#endif

#define APP_FUNCTION_BUTTON 0
#define APP_ONOFF_BUTTON 1

using namespace chip;
using namespace chip::TLV;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::OnOff;
using namespace ::chip::DeviceLayer;
using namespace ::chip::DeviceLayer::Silabs;

namespace {

LEDWidget sOnOffLED;
TimerHandle_t sPlugTimer;

OnOffEffect gEffect = {
    chip::EndpointId{ 1 },
    AppTask::OnTriggerOffWithEffect,
    EffectIdentifierEnum::kDelayedAllOff,
    to_underlying(DelayedAllOffEffectVariantEnum::kDelayedOffFastFade),
};

} // namespace

AppTask AppTask::sAppTask;

CHIP_ERROR AppTask::AppInit()
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    chip::DeviceLayer::Silabs::GetPlatform().SetButtonsCb(AppTask::ButtonEventHandler);

    sPlugTimer = xTimerCreate("plugTmr",
                              pdMS_TO_TICKS(1),
                              false,
                              (void *) this,
                              TimerEventHandler);

    if (sPlugTimer == NULL)
    {
        SILABS_LOG("sPlugTimer timer create failed");
        return APP_ERROR_CREATE_TIMER_FAILED;
    }

    bool currentLedState;
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    OnOffServer::Instance().getOnOffValue(1, &currentLedState);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    mIsOn                = currentLedState;
    mAutoTurnOff         = false;
    mAutoTurnOffDuration = 0;
    mOffEffectArmed      = false;
    mAutoTurnOffTimerArmed = false;

    sOnOffLED.Init(ONOFF_LED);
    sOnOffLED.Set(IsPlugOn());

// Update the LCD with the Stored value. Show QR Code if not provisioned
#ifdef DISPLAY_ENABLED
    GetLCD().WriteDemoUI(IsPlugOn());
#ifdef QR_CODE_ENABLED
#ifdef SL_WIFI
    if (!ConnectivityMgr().IsWiFiStationProvisioned())
#else
    if (!ConnectivityMgr().IsThreadProvisioned())
#endif /* !SL_WIFI */
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
        SILABS_LOG("AppTask.Init() failed");
        appError(err);
    }

#if !(defined(CHIP_CONFIG_ENABLE_ICD_SERVER) && CHIP_CONFIG_ENABLE_ICD_SERVER)
    sAppTask.StartStatusLEDTimer();
#endif

    SILABS_LOG("App Task started");

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

void AppTask::OnOffActionEventHandler(AppEvent * aEvent)
{
    bool initiated = false;
    Action_t action;
    CHIP_ERROR err = CHIP_NO_ERROR;

    if (aEvent->Type == AppEvent::kEventType_Button)
    {
        action = (sAppTask.IsPlugOn()) ? OFF_ACTION : ON_ACTION;
    }
    else
    {
        err = APP_ERROR_UNHANDLED_EVENT;
    }

    if (err == CHIP_NO_ERROR)
    {
        initiated = sAppTask.InitiateAction(aEvent->Type, action);

        if (!initiated)
        {
            SILABS_LOG("Action is already in progress or active.");
        }
    }
}

void AppTask::ButtonEventHandler(uint8_t button, uint8_t btnAction)
{
    AppEvent button_event           = {};
    button_event.Type               = AppEvent::kEventType_Button;
    button_event.ButtonEvent.Action = btnAction;

    if (button == APP_ONOFF_BUTTON && btnAction == static_cast<uint8_t>(SilabsPlatform::ButtonAction::ButtonPressed))
    {
        button_event.Handler = OnOffActionEventHandler;
        sAppTask.PostEvent(&button_event);
    }
    else if (button == APP_FUNCTION_BUTTON)
    {
        button_event.Handler = BaseApplication::ButtonHandler;
        sAppTask.PostEvent(&button_event);
    }
}

void AppTask::ActionCallback(Action_t aAction, int32_t aActor)
{
    bool lightOn = aAction == ON_ACTION;
    SILABS_LOG("Turning light %s", (lightOn) ? "On" : "Off")

    sOnOffLED.Set(lightOn);

#ifdef DISPLAY_ENABLED
    sAppTask.GetLCD().WriteDemoUI(lightOn);
#endif

    if (aAction == ON_ACTION)
    {
        SILABS_LOG("Outlet ON")
    }
    else if (aAction == OFF_ACTION)
    {
        SILABS_LOG("Outlet OFF")
    }

    if (aActor == AppEvent::kEventType_Button)
    {
        TEMPORARY_RETURN_IGNORED PlatformMgr().ScheduleWork(UpdateClusterState, reinterpret_cast<intptr_t>(nullptr));
    }
}

void AppTask::UpdateClusterState(intptr_t context)
{
    uint8_t newValue = sAppTask.IsPlugOn();

    Protocols::InteractionModel::Status status = OnOffServer::Instance().setOnOffValue(1, newValue, false);

    if (status != Protocols::InteractionModel::Status::Success)
    {
        SILABS_LOG("ERR: updating on/off %x", to_underlying(status));
    }
}

bool AppTask::IsPlugOn() const
{
    return mIsOn;
}

void AppTask::EnableAutoTurnOff(bool aOn)
{
    mAutoTurnOff = aOn;
}

void AppTask::SetAutoTurnOffDuration(uint32_t aDurationInSecs)
{
    mAutoTurnOffDuration = aDurationInSecs;
}

bool AppTask::InitiateAction(int32_t aActor, Action_t aAction)
{
    bool action_initiated = false;

    if (((mIsOn == false) || mOffEffectArmed) && aAction == ON_ACTION)
    {
        action_initiated = true;
        if (mOffEffectArmed)
        {
            CancelTimer();
            mOffEffectArmed = false;
        }
    }
    else if (mIsOn && aAction == OFF_ACTION && mOffEffectArmed == false)
    {
        action_initiated = true;
        if (mAutoTurnOffTimerArmed)
        {
            mAutoTurnOffTimerArmed = false;
            CancelTimer();
        }
    }

    if (action_initiated)
    {
        mIsOn = (aAction == ON_ACTION);

        ActionCallback(aAction, aActor);

        if (mAutoTurnOff && mIsOn)
        {
            StartTimer(mAutoTurnOffDuration * 1000);
            mAutoTurnOffTimerArmed = true;
            SILABS_LOG("Auto Turn off enabled. Will be triggered in %u seconds", mAutoTurnOffDuration);
        }
    }

    return action_initiated;
}

void AppTask::StartTimer(uint32_t aTimeoutMs)
{
    if (xTimerIsTimerActive(sPlugTimer))
    {
        SILABS_LOG("app timer already started!");
        CancelTimer();
    }

    if (xTimerChangePeriod(sPlugTimer, pdMS_TO_TICKS(aTimeoutMs), 100) != pdPASS)
    {
        SILABS_LOG("sPlugTimer timer start() failed");
        appError(APP_ERROR_START_TIMER_FAILED);
    }
}

void AppTask::CancelTimer(void)
{
    if (xTimerStop(sPlugTimer, pdMS_TO_TICKS(0)) == pdFAIL)
    {
        SILABS_LOG("sPlugTimer stop() failed");
        appError(APP_ERROR_STOP_TIMER_FAILED);
    }
}

void AppTask::TimerEventHandler(TimerHandle_t xTimer)
{
    AppTask * app = static_cast<AppTask *>(pvTimerGetTimerID(xTimer));

    AppEvent event;
    event.Type               = AppEvent::kEventType_Timer;
    event.TimerEvent.Context = app;
    if (app->mAutoTurnOffTimerArmed)
    {
        event.Handler = AutoTurnOffTimerEventHandler;
    }
    else if (app->mOffEffectArmed)
    {
        event.Handler = OffEffectTimerEventHandler;
    }
    AppTask::GetAppTask().PostEvent(&event);
}

void AppTask::AutoTurnOffTimerEventHandler(AppEvent * aEvent)
{
    AppTask * app = static_cast<AppTask *>(aEvent->TimerEvent.Context);
    int32_t actor = AppEvent::kEventType_Timer;

    if (!app->mAutoTurnOffTimerArmed)
    {
        return;
    }

    app->mAutoTurnOffTimerArmed = false;

    SILABS_LOG("Auto Turn Off has been triggered!");

    app->InitiateAction(actor, OFF_ACTION);
}

void AppTask::OffEffectTimerEventHandler(AppEvent * aEvent)
{
    AppTask * app = static_cast<AppTask *>(aEvent->TimerEvent.Context);
    int32_t actor = AppEvent::kEventType_Timer;

    if (!app->mOffEffectArmed)
    {
        return;
    }

    app->mOffEffectArmed = false;

    SILABS_LOG("OffEffect completed");

    app->InitiateAction(actor, OFF_ACTION);
}

void AppTask::OnTriggerOffWithEffect(OnOffEffect * effect)
{
    auto effectId              = effect->mEffectIdentifier;
    auto effectVariant         = effect->mEffectVariant;
    uint32_t offEffectDuration = 0;

    if (effectId == EffectIdentifierEnum::kDelayedAllOff)
    {
        auto typedEffectVariant = static_cast<DelayedAllOffEffectVariantEnum>(effectVariant);
        if (typedEffectVariant == DelayedAllOffEffectVariantEnum::kDelayedOffFastFade)
        {
            offEffectDuration = 800;
            ChipLogProgress(Zcl, "DelayedAllOffEffectVariantEnum::kDelayedOffFastFade");
        }
        else if (typedEffectVariant == DelayedAllOffEffectVariantEnum::kNoFade)
        {
            offEffectDuration = 800;
            ChipLogProgress(Zcl, "DelayedAllOffEffectVariantEnum::kNoFade");
        }
        else if (typedEffectVariant == DelayedAllOffEffectVariantEnum::kDelayedOffSlowFade)
        {
            offEffectDuration = 12800;
            ChipLogProgress(Zcl, "DelayedAllOffEffectVariantEnum::kDelayedOffSlowFade");
        }
    }
    else if (effectId == EffectIdentifierEnum::kDyingLight)
    {
        auto typedEffectVariant = static_cast<DyingLightEffectVariantEnum>(effectVariant);
        if (typedEffectVariant == DyingLightEffectVariantEnum::kDyingLightFadeOff)
        {
            offEffectDuration = 1500;
            ChipLogProgress(Zcl, "DyingLightEffectVariantEnum::kDyingLightFadeOff");
        }
    }

    sAppTask.mOffEffectArmed = true;
    sAppTask.StartTimer(offEffectDuration);
}

void MatterPostAttributeChangeCallback(const chip::app::ConcreteAttributePath & attributePath, uint8_t type, uint16_t size,
                                       uint8_t * value)
{
    ClusterId clusterId     = attributePath.mClusterId;
    AttributeId attributeId = attributePath.mAttributeId;
    ChipLogProgress(Zcl, "Cluster callback: " ChipLogFormatMEI, ChipLogValueMEI(clusterId));

    if (clusterId == OnOff::Id && attributeId == OnOff::Attributes::OnOff::Id)
    {
#ifdef DIC_ENABLE
        dic_sendmsg("light/state", (const char *) (value ? (*value ? "on" : "off") : "invalid"));
#endif // DIC_ENABLE

        sAppTask.InitiateAction(AppEvent::kEventType_Plug, *value ? AppTask::ON_ACTION : AppTask::OFF_ACTION);
    }
    else if (clusterId == LevelControl::Id)
    {
        ChipLogProgress(Zcl, "Level Control attribute ID: " ChipLogFormatMEI " Type: %u Value: %u, length %u",
                        ChipLogValueMEI(attributeId), type, *value, size);
    }
    else if (clusterId == Identify::Id)
    {
        ChipLogProgress(Zcl, "Identify attribute ID: " ChipLogFormatMEI " Type: %u Value: %u, length %u",
                        ChipLogValueMEI(attributeId), type, *value, size);
    }
}
