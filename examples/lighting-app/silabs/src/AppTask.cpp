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
#include "CommonAppTask.h"
#include "AppConfig.h"
#include "AppEvent.h"

#include "LEDWidget.h"
#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
#include "RGBLEDWidget.h"
#endif //(defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)

#include <app/persistence/AttributePersistenceProviderInstance.h>
#include <app/persistence/DefaultAttributePersistenceProvider.h>
#include <app/persistence/DeferredAttributePersistenceProvider.h>

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app/clusters/on-off-server/on-off-server.h>
#include <app/server/Server.h>
#include <app/util/attribute-storage.h>
#include <setup_payload/OnboardingCodesUtil.h>

#include <clusters/ColorControl/AttributeIds.h>
#include <clusters/LevelControl/AttributeIds.h>
#include <lib/support/TypeTraits.h>

#include <assert.h>

#include <platform/silabs/platformAbstraction/SilabsPlatform.h>

#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <setup_payload/SetupPayload.h>

#include <lib/support/CodeUtils.h>

#include <lib/support/Span.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/silabs/tracing/SilabsTracingMacros.h>

#ifdef SL_CATALOG_SIMPLE_LED_LED1_PRESENT
#define LIGHT_LED 1
#else
#define LIGHT_LED 0
#endif

#define APP_FUNCTION_BUTTON 0
#define APP_LIGHT_SWITCH 1

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace ::chip::app::Clusters::OnOff;
using namespace ::chip::DeviceLayer;
using namespace ::chip::DeviceLayer::Silabs;

namespace {
#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
RGBLEDWidget sLightLED; // Use RGBLEDWidget if RGB LED functionality is enabled
#else
LEDWidget sLightLED; // Use LEDWidget for basic LED functionality
#endif

// Array of attributes that will have their non-volatile storage deferred/delayed.
DeferredAttribute gDeferredAttributeTable[] = {
    DeferredAttribute(ConcreteAttributePath(LIGHT_ENDPOINT, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id)),
    DeferredAttribute(ConcreteAttributePath(LIGHT_ENDPOINT, ColorControl::Id, ColorControl::Attributes::CurrentHue::Id)),
    DeferredAttribute(ConcreteAttributePath(LIGHT_ENDPOINT, ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id))
};

OnOffEffect gEffect = {
    chip::EndpointId(LIGHT_ENDPOINT),
    CommonAppTask::OnTriggerOffWithEffect,
    EffectIdentifierEnum::kDelayedAllOff,
    to_underlying(DelayedAllOffEffectVariantEnum::kDelayedOffFastFade),
};
} // namespace

using namespace chip::TLV;
using namespace ::chip::DeviceLayer;

// Singleton provided by CommonAppTask.cpp (AppTask::GetAppTask() returns CommonAppTask::GetAppTask()).

CHIP_ERROR AppTask::AppInit()
{
    SILABS_LOG("AppTask: default implementation (AppInit)");
    CHIP_ERROR err = CHIP_NO_ERROR;
    chip::DeviceLayer::Silabs::GetPlatform().SetButtonsCb(CommonAppTask::ButtonEventHandler);

    err = InitLight();
    if (err != CHIP_NO_ERROR)
    {
        SILABS_LOG("InitLight() failed");
        appError(err);
    }

    sLightLED.Init(LIGHT_LED);
    sLightLED.Set(IsLightOn());
    SILABS_TRACE_NAMED_INSTANT("LightOn", "Reboot");

// Update the LCD with the Stored value. Show QR Code if not provisioned
#ifdef DISPLAY_ENABLED
    GetLCD().WriteDemoUI(IsLightOn());
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

    BaseApplication::InitCompleteCallback(err);
    return err;
}

CHIP_ERROR AppTask::InitLight()
{
    mLightTimer = osTimerNew(CommonAppTask::LightTimerEventHandler, osTimerOnce, (void *) this, NULL);

    if (mLightTimer == NULL)
    {
        SILABS_LOG("mLightTimer timer create failed");
        return APP_ERROR_CREATE_TIMER_FAILED;
    }

    bool currentLedState;

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    OnOffServer::Instance().getOnOffValue(LIGHT_ENDPOINT, &currentLedState);

#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
    app::DataModel::Nullable<uint8_t> brightness;
    uint16_t currentx, currenty, currentctmireds;
    uint8_t currenthue, currentsaturation;

    if (Clusters::LevelControl::Attributes::CurrentLevel::Get(LIGHT_ENDPOINT, brightness) ==
            Protocols::InteractionModel::Status::Success &&
        !brightness.IsNull())
    {
        mCurrentLevel = brightness.Value();
    }

    if (Clusters::ColorControl::Attributes::CurrentX::Get(LIGHT_ENDPOINT, &currentx) ==
        Protocols::InteractionModel::Status::Success)
    {
        mCurrentX = currentx;
    }
    if (Clusters::ColorControl::Attributes::CurrentY::Get(LIGHT_ENDPOINT, &currenty) ==
        Protocols::InteractionModel::Status::Success)
    {
        mCurrentY = currenty;
    }
    if (Clusters::ColorControl::Attributes::CurrentHue::Get(LIGHT_ENDPOINT, &currenthue) ==
        Protocols::InteractionModel::Status::Success)
    {
        mCurrentHue = currenthue;
    }
    if (Clusters::ColorControl::Attributes::CurrentSaturation::Get(LIGHT_ENDPOINT, &currentsaturation) ==
        Protocols::InteractionModel::Status::Success)
    {
        mCurrentSaturation = currentsaturation;
    }
    if (Clusters::ColorControl::Attributes::ColorTemperatureMireds::Get(LIGHT_ENDPOINT, &currentctmireds) ==
        Protocols::InteractionModel::Status::Success)
    {
        mCurrentCTMireds = currentctmireds;
    }
#endif

    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    mLightState            = currentLedState ? kState_OnCompleted : kState_OffCompleted;
    mAutoTurnOffTimerArmed = false;
    mAutoTurnOff           = false;
    mAutoTurnOffDuration   = 0;
    mOffEffectArmed        = false;

    return CHIP_NO_ERROR;
}

CHIP_ERROR AppTask::StartAppTask()
{
    return BaseApplication::StartAppTask(CommonAppTask::AppTaskMain);
}

void AppTask::AppTaskMain(void * pvParameter)
{
    AppEvent event;
    osMessageQueueId_t sAppEventQueue = *(static_cast<osMessageQueueId_t *>(pvParameter));

    AttributePersistenceProvider * attributePersistence = GetAttributePersistenceProvider();
    VerifyOrDie(attributePersistence != nullptr);

    GetAppTask().pDeferredAttributePersister = new DeferredAttributePersistenceProvider(
        *attributePersistence, Span<DeferredAttribute>(gDeferredAttributeTable, MATTER_ARRAY_SIZE(gDeferredAttributeTable)),
        System::Clock::Milliseconds32(SL_MATTER_DEFERRED_ATTRIBUTE_STORE_DELAY_MS));
    VerifyOrDie(GetAppTask().pDeferredAttributePersister != nullptr);

    app::SetAttributePersistenceProvider(GetAppTask().pDeferredAttributePersister);

    CHIP_ERROR err = GetAppTask().Init();
    if (err != CHIP_NO_ERROR)
    {
        SILABS_LOG("AppTask.Init() failed");
        appError(err);
    }

#if !(defined(CHIP_CONFIG_ENABLE_ICD_SERVER) && CHIP_CONFIG_ENABLE_ICD_SERVER)
    GetAppTask().StartStatusLEDTimer();
#endif

    SILABS_LOG("App Task started");

    while (true)
    {
        osStatus_t eventReceived = osMessageQueueGet(sAppEventQueue, &event, nullptr, osWaitForever);
        while (eventReceived == osOK)
        {
            GetAppTask().DispatchEvent(&event);
            eventReceived = osMessageQueueGet(sAppEventQueue, &event, nullptr, 0);
        }
    }
}

void AppTask::LightActionEventHandler(AppEvent * aEvent)
{
    bool initiated = false;
    AppTask::Action_t action;
    int32_t actor;
    uint8_t value  = aEvent->LightEvent.Value;
    CHIP_ERROR err = CHIP_NO_ERROR;

    if (aEvent->Type == AppEvent::kEventType_Light)
    {
        action = static_cast<AppTask::Action_t>(aEvent->LightEvent.Action);
        actor  = aEvent->LightEvent.Actor;
    }
    else if (aEvent->Type == AppEvent::kEventType_Button)
    {
        action = (CommonAppTask::GetAppTask().IsLightOn()) ? OFF_ACTION : ON_ACTION;
        actor  = AppEvent::kEventType_Button;
    }
    else
    {
        err = APP_ERROR_UNHANDLED_EVENT;
    }

    if (err == CHIP_NO_ERROR)
    {
        initiated = CommonAppTask::GetAppTask().InitiateAction(actor, action, &value);

        if (!initiated)
        {
            SILABS_LOG("Action is already in progress or active.");
        }
    }
}

#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
void AppTask::LightControlEventHandler(AppEvent * aEvent)
{
    uint8_t light_action                = aEvent->LightControlEvent.Action;
    RGBLEDWidget::ColorData_t colorData = aEvent->LightControlEvent.Value;
    PlatformMgr().LockChipStack();
    Protocols::InteractionModel::Status status;
    app::DataModel::Nullable<uint8_t> currentlevel;
    status = LevelControl::Attributes::CurrentLevel::Get(LIGHT_ENDPOINT, currentlevel);
    PlatformMgr().UnlockChipStack();
    VerifyOrReturn(Protocols::InteractionModel::Status::Success == status,
                ChipLogError(NotSpecified, "Failed to get CurrentLevel attribute"));
    if (status == Protocols::InteractionModel::Status::Success && !currentlevel.IsNull())
    {
        sLightLED.SetLevel(currentlevel.Value());
    }
    switch (light_action)
    {
    case COLOR_ACTION_XY: {
        sLightLED.SetColorFromXY(colorData.xy.x, colorData.xy.y);
    }
    break;
    case COLOR_ACTION_HSV: {
        sLightLED.SetColorFromHSV(colorData.hsv.h, colorData.hsv.s);
    }
    break;
    case COLOR_ACTION_CT: {
        sLightLED.SetColorFromCT(colorData.ct.ctMireds);
    }
    break;
    default:
        ChipLogProgress(NotSpecified, "AppTask: unknown light action");
        break;
    }
}
#endif

void AppTask::ButtonEventHandler(uint8_t button, uint8_t btnAction)
{
    SILABS_LOG("AppTask: default implementation (ButtonEventHandler)");
    AppEvent button_event           = {};
    button_event.Type               = AppEvent::kEventType_Button;
    button_event.ButtonEvent.Action = btnAction;

    if (button == APP_LIGHT_SWITCH && btnAction == static_cast<uint8_t>(SilabsPlatform::ButtonAction::ButtonPressed))
    {
        button_event.Handler = CommonAppTask::LightActionEventHandler;
        AppTask::GetAppTask().PostEvent(&button_event);
    }
    else if (button == APP_FUNCTION_BUTTON)
    {
        button_event.Handler = BaseApplication::ButtonHandler;
        AppTask::GetAppTask().PostEvent(&button_event);
    }
}

void AppTask::OnLightActionInitiated(AppTask::Action_t aAction, int32_t aActor, uint8_t * aValue)
{
    SILABS_LOG("AppTask: default implementation (OnLightActionInitiated)");
    if (aAction == LEVEL_ACTION)
    {
        VerifyOrReturn(aValue != nullptr);
        sLightLED.SetLevel(*aValue);
    }
    else
    {
        bool lightOn = aAction == ON_ACTION;
        SILABS_LOG("Turning light %s", (lightOn) ? "On" : "Off")

        sLightLED.Set(lightOn);

#ifdef DISPLAY_ENABLED
        GetLCD().WriteDemoUI(lightOn);
#endif

        if (aActor == AppEvent::kEventType_Button)
        {
            mSyncClusterToButtonAction = true;
        }
    }
}

void AppTask::OnLightActionCompleted(AppTask::Action_t aAction)
{
    SILABS_LOG("AppTask: default implementation (OnLightActionCompleted)");
    if (aAction == ON_ACTION)
    {
        SILABS_LOG("Light ON")
    }
    else if (aAction == OFF_ACTION)
    {
        SILABS_LOG("Light OFF")
    }

    if (mSyncClusterToButtonAction)
    {
        TEMPORARY_RETURN_IGNORED chip::DeviceLayer::PlatformMgr().ScheduleWork(UpdateClusterState,
                                                                            reinterpret_cast<intptr_t>(nullptr));
        mSyncClusterToButtonAction = false;
    }
}

bool AppTask::IsActionInProgress() const
{
    return (mLightState == kState_OffInitiated || mLightState == kState_OnInitiated);
}

bool AppTask::IsLightOn() const
{
    return (mLightState == kState_OnCompleted);
}

void AppTask::EnableAutoTurnOff(bool aOn)
{
    mAutoTurnOff = aOn;
}

void AppTask::SetAutoTurnOffDuration(uint32_t aDurationInSecs)
{
    mAutoTurnOffDuration = aDurationInSecs;
}

bool AppTask::InitiateAction(int32_t aActor, AppTask::Action_t aAction, uint8_t * aValue)
{
    bool action_initiated = false;
    AppTask::State_t new_state;

    if (((mLightState == kState_OffCompleted) || mOffEffectArmed) && aAction == ON_ACTION)
    {
        action_initiated = true;
        new_state        = kState_OnInitiated;
        if (mOffEffectArmed)
        {
            CancelLightTimer();
            mOffEffectArmed = false;
        }
    }
    else if (mLightState == kState_OnCompleted && aAction == OFF_ACTION && mOffEffectArmed == false)
    {
        action_initiated = true;
        new_state        = kState_OffInitiated;
        if (mAutoTurnOffTimerArmed)
        {
            mAutoTurnOffTimerArmed = false;
            CancelLightTimer();
        }
    }
    else if (aAction == LEVEL_ACTION)
    {
        action_initiated = true;
    }

    if (action_initiated && (aAction == ON_ACTION || aAction == OFF_ACTION))
    {
        StartLightTimer(ACTUATOR_MOVEMENT_PERIOD_MS);
        mLightState = new_state;
    }

    if (action_initiated)
    {
        OnLightActionInitiated(aAction, aActor, aValue);
    }

    return action_initiated;
}

void AppTask::StartLightTimer(uint32_t aTimeoutMs)
{
    if (osTimerStart(mLightTimer, pdMS_TO_TICKS(aTimeoutMs)) != osOK)
    {
        SILABS_LOG("mLightTimer timer start() failed");
        appError(APP_ERROR_START_TIMER_FAILED);
    }
}

void AppTask::CancelLightTimer()
{
    if (osTimerStop(mLightTimer) == osError)
    {
        SILABS_LOG("mLightTimer stop() failed");
        appError(APP_ERROR_STOP_TIMER_FAILED);
    }
}

void AppTask::LightTimerEventHandler(void * timerCbArg)
{
    AppTask * task = static_cast<AppTask *>(timerCbArg);

    AppEvent event;
    event.Type               = AppEvent::kEventType_Timer;
    event.TimerEvent.Context = task;
    if (task->mAutoTurnOffTimerArmed)
    {
        event.Handler = AutoTurnOffTimerEventHandler;
    }
    else if (task->mOffEffectArmed)
    {
        event.Handler = OffEffectTimerEventHandler;
    }
    else
    {
        event.Handler = ActuatorMovementTimerEventHandler;
    }
    AppTask::GetAppTask().PostEvent(&event);
}

void AppTask::AutoTurnOffTimerEventHandler(AppEvent * aEvent)
{
    AppTask * task = static_cast<AppTask *>(aEvent->TimerEvent.Context);
    int32_t actor  = AppEvent::kEventType_Timer;
    uint8_t value  = 0;

    if (!task->mAutoTurnOffTimerArmed)
    {
        return;
    }

    task->mAutoTurnOffTimerArmed = false;

    SILABS_LOG("Auto Turn Off has been triggered!");

    CommonAppTask::GetAppTask().InitiateAction(actor, OFF_ACTION, &value);
}

void AppTask::OffEffectTimerEventHandler(AppEvent * aEvent)
{
    AppTask * task = static_cast<AppTask *>(aEvent->TimerEvent.Context);
    int32_t actor  = AppEvent::kEventType_Timer;
    uint8_t value  = 0;

    if (!task->mOffEffectArmed)
    {
        return;
    }

    task->mOffEffectArmed = false;

    SILABS_LOG("OffEffect completed");

    CommonAppTask::GetAppTask().InitiateAction(actor, OFF_ACTION, &value);
}

void AppTask::ActuatorMovementTimerEventHandler(AppEvent * aEvent)
{
    AppTask::Action_t actionCompleted = INVALID_ACTION;

    AppTask * task = static_cast<AppTask *>(aEvent->TimerEvent.Context);

    if (task->mLightState == kState_OffInitiated)
    {
        task->mLightState = kState_OffCompleted;
        actionCompleted   = OFF_ACTION;
    }
    else if (task->mLightState == kState_OnInitiated)
    {
        task->mLightState = kState_OnCompleted;
        actionCompleted   = ON_ACTION;
    }

    if (actionCompleted != INVALID_ACTION)
    {
        task->OnLightActionCompleted(actionCompleted);

        if (task->mAutoTurnOff && actionCompleted == ON_ACTION)
        {
            task->StartLightTimer(task->mAutoTurnOffDuration * 1000);
            task->mAutoTurnOffTimerArmed = true;
            SILABS_LOG("Auto Turn off enabled. Will be triggered in %u seconds", task->mAutoTurnOffDuration);
        }
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

    AppTask::GetAppTask().mOffEffectArmed = true;
    AppTask::GetAppTask().StartLightTimer(offEffectDuration);
}

#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
bool AppTask::InitiateLightCtrlAction(int32_t aActor, AppTask::Action_t aAction, uint32_t aAttributeId, uint8_t * value)
{
    bool action_initiated = false;
    VerifyOrReturnError(aAction == COLOR_ACTION_XY || aAction == COLOR_ACTION_HSV || aAction == COLOR_ACTION_CT, action_initiated);
    RGBLEDWidget::ColorData_t colorData;
    switch (aAction)
    {
    case COLOR_ACTION_XY:
        colorData.xy = { mCurrentX, mCurrentY };

        if (aAttributeId == ColorControl::Attributes::CurrentX::Id)
        {
            VerifyOrReturnValue(colorData.xy.x != *reinterpret_cast<uint16_t *>(value), action_initiated);
            colorData.xy.x   = *reinterpret_cast<uint16_t *>(value);
            action_initiated = true;
        }
        else if (aAttributeId == ColorControl::Attributes::CurrentY::Id)
        {
            VerifyOrReturnValue(colorData.xy.y != *reinterpret_cast<uint16_t *>(value), action_initiated);
            colorData.xy.y   = *reinterpret_cast<uint16_t *>(value);
            action_initiated = true;
        }
        break;

    case COLOR_ACTION_HSV:
        colorData.hsv = { mCurrentHue, mCurrentSaturation };

        if (aAttributeId == ColorControl::Attributes::CurrentHue::Id)
        {
            VerifyOrReturnValue(colorData.hsv.h != *value, action_initiated);
            colorData.hsv.h  = *value;
            mCurrentHue      = *value;
            action_initiated = true;
        }
        else if (aAttributeId == ColorControl::Attributes::CurrentSaturation::Id)
        {
            VerifyOrReturnValue(colorData.hsv.s != *value, action_initiated);
            colorData.hsv.s    = *value;
            mCurrentSaturation = *value;
            action_initiated   = true;
        }
        break;

    case COLOR_ACTION_CT:
        colorData.ct.ctMireds = mCurrentCTMireds;
        VerifyOrReturnValue(colorData.ct.ctMireds != *(uint16_t *) value, action_initiated);
        colorData.ct.ctMireds = *(uint16_t *) value;
        action_initiated      = true;
        break;

    default:
        break;
    }
    PostLightControlActionRequest(aActor, aAction, (RGBLEDWidget::ColorData_t *) &colorData);
    return action_initiated;
}
#endif

void AppTask::PostLightActionRequest(int32_t aActor, AppTask::Action_t aAction)
{
    AppEvent event;
    event.Type              = AppEvent::kEventType_Light;
    event.LightEvent.Actor  = aActor;
    event.LightEvent.Action = aAction;
    event.Handler           = CommonAppTask::LightActionEventHandler;
    PostEvent(&event);
}

#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
void AppTask::PostLightControlActionRequest(int32_t aActor, AppTask::Action_t aAction, RGBLEDWidget::ColorData_t * aValue)
{
    AppEvent light_event;
    light_event.Type                     = AppEvent::kEventType_Light;
    light_event.LightControlEvent.Actor  = aActor;
    light_event.LightControlEvent.Action = aAction;
    light_event.LightControlEvent.Value  = *aValue;
    light_event.Handler                  = LightControlEventHandler;
    PostEvent(&light_event);
}
#endif

void AppTask::UpdateClusterState(intptr_t context)
{
    uint8_t newValue = CommonAppTask::GetAppTask().IsLightOn();

    Protocols::InteractionModel::Status status = OnOffServer::Instance().setOnOffValue(LIGHT_ENDPOINT, newValue, false);

    if (status != Protocols::InteractionModel::Status::Success)
    {
        SILABS_LOG("ERR: updating on/off %x", to_underlying(status));
    }
}
