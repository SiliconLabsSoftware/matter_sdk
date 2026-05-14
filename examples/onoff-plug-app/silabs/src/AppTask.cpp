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
#include "CustomerAppTask.h"
#include "LEDWidget.h"

#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/clusters/on-off-server/on-off-server.h>
#include <app/server/Server.h>
#include <app/util/attribute-storage.h>

#include <setup_payload/OnboardingCodesUtil.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <setup_payload/SetupPayload.h>

#include <lib/support/CodeUtils.h>
#include <lib/support/TypeTraits.h>
#include <lib/support/logging/CHIPLogging.h>

#include <platform/CHIPDeviceLayer.h>

#include <platform/silabs/platformAbstraction/SilabsPlatform.h>
#include <platform/silabs/tracing/SilabsTracingMacros.h>

#ifdef SL_MATTER_ENABLE_AWS
#include "MatterAws.h"
#endif // SL_MATTER_ENABLE_AWS

#ifdef SL_CATALOG_SIMPLE_LED_LED1_PRESENT
#define ONOFF_LED 1
#else
#define ONOFF_LED 0
#endif

#define APP_FUNCTION_BUTTON 0
#define APP_ONOFF_BUTTON 1

using namespace chip;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::OnOff;
using namespace chip::DeviceLayer;
using namespace chip::DeviceLayer::Silabs;

namespace {

CustomerAppTask & appInstance()
{
    return CustomerAppTask::GetAppTask();
}

LEDWidget sOnOffLED;
osTimerId_t sPlugTimer = nullptr;

bool sPlugOn = false;
bool sOffEffectArmed = false;

void OffEffectTimerEventHandler(AppEvent * /* aEvent */)
{
    if (!sOffEffectArmed)
    {
        return;
    }
    sOffEffectArmed = false;

    sPlugOn = false;
    sOnOffLED.Set(false);
#ifdef DISPLAY_ENABLED
    BaseApplication::GetLCD().WriteDemoUI(false);
#endif
}

OnOffEffect gEffect = {
    chip::EndpointId(ONOFF_PLUG_ENDPOINT),
    &CustomerAppTask::OnTriggerOffWithEffect,
    EffectIdentifierEnum::kDelayedAllOff,
    to_underlying(DelayedAllOffEffectVariantEnum::kDelayedOffFastFade),
};

} // namespace

void AppTask::UpdateClusterState(intptr_t context)
{
    Protocols::InteractionModel::Status status =
        OnOffServer::Instance().setOnOffValue(ONOFF_PLUG_ENDPOINT, static_cast<uint8_t>(context), false);

    if (status != Protocols::InteractionModel::Status::Success)
    {
        SILABS_LOG("ERR: updating on/off %x", to_underlying(status));
    }
}

void AppTask::TimerEventHandler(void * /* timerCbArg */)
{
    AppEvent event{};
    event.Type               = AppEvent::kEventType_Timer;
    event.TimerEvent.Context = nullptr;
    event.Handler            = &OffEffectTimerEventHandler;
    appInstance().PostEvent(&event);
}

CHIP_ERROR AppTask::InitPlug()
{
    sPlugTimer = osTimerNew(&CustomerAppTask::TimerEventHandler, osTimerOnce, nullptr, nullptr);

    if (sPlugTimer == nullptr)
    {
        SILABS_LOG("sPlugTimer timer create failed");
        return APP_ERROR_CREATE_TIMER_FAILED;
    }

    bool currentLedState;
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    OnOffServer::Instance().getOnOffValue(ONOFF_PLUG_ENDPOINT, &currentLedState);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    sPlugOn = currentLedState;

    return CHIP_NO_ERROR;
}

CHIP_ERROR AppTask::AppInit()
{
    chip::DeviceLayer::Silabs::GetPlatform().SetButtonsCb(&CustomerAppTask::ButtonEventHandler);
    CHIP_ERROR err = appInstance().InitPlug();

    if (err != CHIP_NO_ERROR)
    {
        SILABS_LOG("InitPlug() failed");
        appError(err);
    }

    sOnOffLED.Init(ONOFF_LED);
    sOnOffLED.Set(sPlugOn);

// Update the LCD with the Stored value. Show QR Code if not provisioned
    #ifdef DISPLAY_ENABLED
    GetLCD().WriteDemoUI(sPlugOn);
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
    return BaseApplication::StartAppTask(&AppTask::AppTaskMain);
}

void AppTask::AppTaskMain(void * pvParameter)
{
    AppEvent event;
    osMessageQueueId_t sAppEventQueue = *(static_cast<osMessageQueueId_t *>(pvParameter));

    CHIP_ERROR err = appInstance().Init();
    if (err != CHIP_NO_ERROR)
    {
        SILABS_LOG("AppTask.Init() failed");
        appError(err);
    }

#if !(defined(CHIP_CONFIG_ENABLE_ICD_SERVER) && CHIP_CONFIG_ENABLE_ICD_SERVER)
    appInstance().StartStatusLEDTimer();
#endif

    SILABS_LOG("App Task started");

    while (true)
    {
        osStatus_t eventReceived = osMessageQueueGet(sAppEventQueue, &event, nullptr, osWaitForever);
        while (eventReceived == osOK)
        {
            appInstance().DispatchEvent(&event);
            eventReceived = osMessageQueueGet(sAppEventQueue, &event, nullptr, 0);
        }
    }
}

void AppTask::OnOffActionEventHandler(AppEvent * aEvent)
{
    if (aEvent->Type != AppEvent::kEventType_Button)
    {
        return;
    }

    sPlugOn = !sPlugOn;
    sOnOffLED.Set(sPlugOn);

    sOffEffectArmed = false;
    if (osTimerIsRunning(sPlugTimer))
    {
        if (osTimerStop(sPlugTimer) == osError)
        {
            SILABS_LOG("sPlugTimer stop() failed");
            appError(APP_ERROR_STOP_TIMER_FAILED);
        }
    }

#ifdef DISPLAY_ENABLED
    BaseApplication::GetLCD().WriteDemoUI(sPlugOn);
#endif

    TEMPORARY_RETURN_IGNORED chip::DeviceLayer::PlatformMgr().ScheduleWork(UpdateClusterState,
                                                                           static_cast<intptr_t>(sPlugOn));
}

void AppTask::ButtonEventHandler(uint8_t button, uint8_t btnAction)
{
    AppEvent button_event           = {};
    button_event.Type               = AppEvent::kEventType_Button;
    button_event.ButtonEvent.Action = btnAction;

    if (button == APP_ONOFF_BUTTON && btnAction == static_cast<uint8_t>(SilabsPlatform::ButtonAction::ButtonPressed))
    {
        button_event.Handler = &CustomerAppTask::OnOffActionEventHandler;
        appInstance().PostEvent(&button_event);
    }
    else if (button == APP_FUNCTION_BUTTON)
    {
        button_event.Handler = BaseApplication::ButtonHandler;
        appInstance().PostEvent(&button_event);
    }
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

    if (offEffectDuration == 0)
    {
        ChipLogProgress(Zcl, "OffWithEffect: unsupported effect, completing immediately");
        sOffEffectArmed = true;
        AppEvent event{};
        event.Type               = AppEvent::kEventType_Timer;
        event.TimerEvent.Context = nullptr;
        event.Handler            = &OffEffectTimerEventHandler;
        appInstance().PostEvent(&event);
        return;
    }

    if (osTimerStart(sPlugTimer, pdMS_TO_TICKS(offEffectDuration)) != osOK)
    {
        SILABS_LOG("sPlugTimer timer start() failed");
        appError(APP_ERROR_START_TIMER_FAILED);
    }
    sOffEffectArmed = true;
}

void AppTask::DMPostAttributeChangeCallback(const chip::app::ConcreteAttributePath & attributePath, uint8_t type, uint16_t size,
                                            uint8_t * value)
{
    ClusterId clusterId     = attributePath.mClusterId;
    AttributeId attributeId = attributePath.mAttributeId;
    ChipLogProgress(Zcl, "Cluster callback: " ChipLogFormatMEI, ChipLogValueMEI(clusterId));

    switch (clusterId)
    {
    case OnOff::Id:
        if (attributeId == OnOff::Attributes::OnOff::Id && value != nullptr && size == sizeof(uint8_t))
        {
#ifdef SL_MATTER_ENABLE_AWS
            ChipLogProgress(Zcl, "sending light state update");
            MatterAwsSendMsg("light/state", (const char *) (value ? (*value ? "on" : "off") : "invalid"));
#endif // SL_MATTER_ENABLE_AWS

            const bool plugOn = (*value != 0);
            if (!plugOn && sOffEffectArmed)
            {
                break;
            }

            if (plugOn)
            {
                sOffEffectArmed = false;
            }

            sPlugOn = plugOn;
            sOnOffLED.Set(sPlugOn);
#ifdef DISPLAY_ENABLED
            BaseApplication::GetLCD().WriteDemoUI(sPlugOn);
#endif
            if (plugOn && osTimerIsRunning(sPlugTimer))
            {
                if (osTimerStop(sPlugTimer) == osError)
                {
                    SILABS_LOG("sPlugTimer stop() failed");
                    appError(APP_ERROR_STOP_TIMER_FAILED);
                }
            }
        }
        break;

    case LevelControl::Id:
        ChipLogProgress(Zcl, "Level Control attribute ID: " ChipLogFormatMEI " Type: %u Value: %u, length %u",
                        ChipLogValueMEI(attributeId), type, value != nullptr ? static_cast<unsigned>(*value) : 0u, size);
        break;

    case Identify::Id:
        if (value != nullptr && size == sizeof(uint8_t))
        {
            ChipLogProgress(Zcl, "Identify attribute ID: " ChipLogFormatMEI " Type: %u Value: %u, length %u",
                            ChipLogValueMEI(attributeId), type, *value, size);
        }
        break;

    default:
        break;
    }
}
