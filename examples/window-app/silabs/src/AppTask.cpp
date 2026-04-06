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

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app-common/zap-generated/callback.h>
#include <app-common/zap-generated/cluster-objects.h>
#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/CommandHandler.h>
#include <app/ConcreteAttributePath.h>
#include <app/ConcreteCommandPath.h>
#include <app/clusters/window-covering-server/window-covering-server.h>
#include <app/server/Server.h>
#include <app/util/attribute-storage.h>

#include <assert.h>
#include <cmsis_os2.h>
#include <lib/core/CHIPError.h>
#include <lib/dnssd/Advertiser.h>
#include <lib/support/CodeUtils.h>
#include <platform/CHIPDeviceLayer.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <setup_payload/SetupPayload.h>

#include <platform/silabs/platformAbstraction/SilabsPlatform.h>

#ifdef SL_WIFI
#include <app/clusters/network-commissioning/network-commissioning.h>
#include <platform/silabs/NetworkCommissioningWiFiDriver.h>
#include <platform/silabs/wifi/WifiInterface.h>
#endif

#ifdef DISPLAY_ENABLED
#include <LcdPainter.h>
#endif

#ifdef SL_MATTER_ENABLE_AWS
#define DECIMAL 10
#define MSG_SIZE 6
#include "MatterAws.h"
#endif

#define LCD_ICON_TIMEOUT 1000
#define APP_ACTION_LED 1

using namespace chip::app::Clusters::WindowCovering;
using namespace chip;
using namespace ::chip::DeviceLayer;
using namespace ::chip::DeviceLayer::Silabs;

namespace {
constexpr uint8_t kNamespacePosition = 8;
constexpr uint8_t kTagPositionLeft   = 0;
constexpr uint8_t kTagPositionRight  = 1;

const chip::app::Clusters::Descriptor::Structs::SemanticTagStruct::Type kEndpoint1TagList[] = {
    { .namespaceID = kNamespacePosition, .tag = kTagPositionLeft },
};

const chip::app::Clusters::Descriptor::Structs::SemanticTagStruct::Type kEndpoint2TagList[] = {
    { .namespaceID = kNamespacePosition, .tag = kTagPositionRight },
};
} // namespace

// --- Helper ---

static AppEvent CreateNewEvent(AppEvent::AppEventTypes type)
{
    AppEvent aEvent;
    aEvent.Type                = type;
    aEvent.Handler             = AppTask::GeneralEventHandler;
    aEvent.WindowEvent.Context = &AppTask::GetAppTask();
    return aEvent;
}

// =============================================================================
// Timer
// =============================================================================

AppTask::Timer::Timer(uint32_t timeoutInMs, Callback callback, void * context) : mCallback(callback), mContext(context)
{
    mHandler = osTimerNew(TimerCallback, osTimerOnce, this, NULL);

    if (mHandler == NULL)
    {
        SILABS_LOG("Timer create failed");
        appError(APP_ERROR_CREATE_TIMER_FAILED);
    }
}

AppTask::Timer::~Timer()
{
    if (mHandler)
    {
        osTimerDelete(mHandler);
        mHandler = nullptr;
    }
}

void AppTask::Timer::Start()
{
    osStatus_t status = osTimerStart(mHandler, pdMS_TO_TICKS(100));
    if (status != osOK)
    {
        SILABS_LOG("Timer start() failed with error %ld", status);
        appError(APP_ERROR_START_TIMER_FAILED);
    }

    mIsActive = true;
}

void AppTask::Timer::Stop()
{
    if (osTimerStop(mHandler) == osError)
    {
        SILABS_LOG("Timer stop() failed");
        appError(APP_ERROR_STOP_TIMER_FAILED);
    }
    mIsActive = false;
}

void AppTask::Timer::Timeout()
{
    mIsActive = false;
    if (mCallback)
    {
        mCallback(*this);
    }
}

void AppTask::Timer::TimerCallback(void * timerCbArg)
{
    Timer * timer = static_cast<Timer *>(timerCbArg);
    if (timer)
    {
        timer->Timeout();
    }
}

// =============================================================================
// Cover
// =============================================================================

void AppTask::Cover::Init(chip::EndpointId endpoint)
{
    mEndpoint  = endpoint;
    mLiftTimer = new Timer(COVER_LIFT_TILT_TIMEOUT, OnLiftTimeout, this);
    mTiltTimer = new Timer(COVER_LIFT_TILT_TIMEOUT, OnTiltTimeout, this);

    Attributes::InstalledOpenLimitLift::Set(endpoint, LIFT_OPEN_LIMIT);
    Attributes::InstalledClosedLimitLift::Set(endpoint, LIFT_CLOSED_LIMIT);

    Attributes::InstalledOpenLimitTilt::Set(endpoint, TILT_OPEN_LIMIT);
    Attributes::InstalledClosedLimitTilt::Set(endpoint, TILT_CLOSED_LIMIT);

    TypeSet(endpoint, Type::kTiltBlindLiftAndTilt);

    chip::BitMask<ConfigStatus> configStatus = ConfigStatusGet(endpoint);
    configStatus.Set(ConfigStatus::kLiftEncoderControlled);
    configStatus.Set(ConfigStatus::kTiltEncoderControlled);
    ConfigStatusSet(endpoint, configStatus);

    chip::app::Clusters::WindowCovering::ConfigStatusUpdateFeatures(endpoint);

    EndProductTypeSet(endpoint, EndProductType::kInteriorBlind);

    chip::BitMask<Mode> mode;
    mode.Clear(Mode::kMotorDirectionReversed);
    mode.Clear(Mode::kMaintenanceMode);
    mode.Clear(Mode::kCalibrationMode);
    mode.Set(Mode::kLedFeedback);

    ModeSet(endpoint, mode);

    chip::BitFlags<SafetyStatus> safetyStatus(0x00);
}

void AppTask::Cover::ScheduleControlAction(ControlAction action, bool setNewTarget)
{
    VerifyOrReturn(action <= ControlAction::Tilt);
    CoverWorkData * data = new CoverWorkData(this, setNewTarget);
    VerifyOrDie(data != nullptr);

    AsyncWorkFunct workFunct = (action == ControlAction::Lift) ? LiftUpdateWorker : TiltUpdateWorker;
    if (PlatformMgr().ScheduleWork(workFunct, reinterpret_cast<intptr_t>(data)) != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "Failed to schedule cover control action");
        delete data;
    }
}

void AppTask::Cover::LiftUpdateWorker(intptr_t arg)
{
    std::unique_ptr<CoverWorkData> data(reinterpret_cast<CoverWorkData *>(arg));
    Cover * cover = data->cover;

    NPercent100ths current, target;

    VerifyOrReturn(Attributes::TargetPositionLiftPercent100ths::Get(cover->mEndpoint, target) ==
                   Protocols::InteractionModel::Status::Success);
    VerifyOrReturn(Attributes::CurrentPositionLiftPercent100ths::Get(cover->mEndpoint, current) ==
                   Protocols::InteractionModel::Status::Success);

    OperationalState opState = ComputeOperationalState(target, current);

    if (data->setNewTarget)
    {
        cover->mLiftTimer->Stop();
        cover->mLiftOpState = opState;
    }

    if (cover->mLiftOpState == opState)
    {
        chip::Percent100ths percent100ths;

        if (!current.IsNull())
        {
            percent100ths = ComputePercent100thsStep(cover->mLiftOpState, current.Value(), LIFT_DELTA);
        }
        else
        {
            percent100ths = WC_PERCENT100THS_MIDDLE;
        }

        cover->PositionSet(cover->mEndpoint, percent100ths, ControlAction::Lift);
    }
    else
    {
        if (!target.IsNull())
            cover->PositionSet(cover->mEndpoint, target.Value(), ControlAction::Lift);

        cover->mLiftOpState = OperationalState::Stall;
    }

    OperationalStateSet(cover->mEndpoint, OperationalStatus::kLift, cover->mLiftOpState);

    if ((OperationalState::Stall != cover->mLiftOpState) && cover->mLiftTimer)
    {
        cover->mLiftTimer->Start();
    }
}

void AppTask::Cover::TiltUpdateWorker(intptr_t arg)
{
    std::unique_ptr<CoverWorkData> data(reinterpret_cast<CoverWorkData *>(arg));
    Cover * cover = data->cover;

    NPercent100ths current, target;
    VerifyOrReturn(Attributes::TargetPositionTiltPercent100ths::Get(cover->mEndpoint, target) ==
                   Protocols::InteractionModel::Status::Success);
    VerifyOrReturn(Attributes::CurrentPositionTiltPercent100ths::Get(cover->mEndpoint, current) ==
                   Protocols::InteractionModel::Status::Success);

    OperationalState opState = ComputeOperationalState(target, current);

    if (data->setNewTarget)
    {
        cover->mTiltTimer->Stop();
        cover->mTiltOpState = opState;
    }

    if (cover->mTiltOpState == opState)
    {
        chip::Percent100ths percent100ths;

        if (!current.IsNull())
        {
            percent100ths = ComputePercent100thsStep(cover->mTiltOpState, current.Value(), TILT_DELTA);
        }
        else
        {
            percent100ths = WC_PERCENT100THS_MIDDLE;
        }

        cover->PositionSet(cover->mEndpoint, percent100ths, ControlAction::Tilt);
    }
    else
    {
        if (!target.IsNull())
            cover->PositionSet(cover->mEndpoint, target.Value(), ControlAction::Tilt);

        cover->mTiltOpState = OperationalState::Stall;
    }

    OperationalStateSet(cover->mEndpoint, OperationalStatus::kTilt, cover->mTiltOpState);

    if ((OperationalState::Stall != cover->mTiltOpState) && cover->mTiltTimer)
    {
        cover->mTiltTimer->Start();
    }
}

void AppTask::Cover::UpdateTargetPosition(OperationalState direction, ControlAction action)
{
    Protocols::InteractionModel::Status status;
    NPercent100ths current;
    chip::Percent100ths target;

    chip::DeviceLayer::PlatformMgr().LockChipStack();

    if (action == ControlAction::Tilt)
    {
        status = Attributes::CurrentPositionTiltPercent100ths::Get(mEndpoint, current);
        if ((status == Protocols::InteractionModel::Status::Success) && !current.IsNull())
        {
            target = ComputePercent100thsStep(direction, current.Value(), TILT_DELTA);
            (void) Attributes::TargetPositionTiltPercent100ths::Set(mEndpoint, target);
        }
    }
    else
    {
        status = Attributes::CurrentPositionLiftPercent100ths::Get(mEndpoint, current);
        if ((status == Protocols::InteractionModel::Status::Success) && !current.IsNull())
        {
            target = ComputePercent100thsStep(direction, current.Value(), LIFT_DELTA);
            (void) Attributes::TargetPositionLiftPercent100ths::Set(mEndpoint, target);
        }
    }
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
}

Type AppTask::Cover::CycleType()
{
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    Type type = TypeGet(mEndpoint);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    switch (type)
    {
    case Type::kRollerShade:
        type = Type::kDrapery;
        break;
    case Type::kDrapery:
        type = Type::kTiltBlindLiftAndTilt;
        break;
    case Type::kTiltBlindLiftAndTilt:
        type = Type::kRollerShade;
        break;
    default:
        type = Type::kTiltBlindLiftAndTilt;
    }

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    TypeSet(mEndpoint, type);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    return type;
}

void AppTask::Cover::OnLiftTimeout(AppTask::Timer & timer)
{
    AppTask::Cover * cover = static_cast<AppTask::Cover *>(timer.mContext);
    if (cover)
    {
        cover->LiftContinueToTarget();
    }
}

void AppTask::Cover::OnTiltTimeout(AppTask::Timer & timer)
{
    AppTask::Cover * cover = static_cast<AppTask::Cover *>(timer.mContext);
    if (cover)
    {
        cover->TiltContinueToTarget();
    }
}

void AppTask::Cover::PositionSet(chip::EndpointId endpointId, chip::Percent100ths position, ControlAction action)
{
    NPercent100ths nullablePosition;
    nullablePosition.SetNonNull(position);
    if (action == ControlAction::Tilt)
    {
        TiltPositionSet(endpointId, nullablePosition);
#ifdef SL_MATTER_ENABLE_AWS
        uint16_t value = position;
        char buffer[MSG_SIZE];
        itoa(value, buffer, DECIMAL);
        MatterAwsSendMsg("tilt/position set", (const char *) (buffer));
#endif
    }
    else
    {
        LiftPositionSet(endpointId, nullablePosition);
#ifdef SL_MATTER_ENABLE_AWS
        uint16_t value = position;
        char buffer[MSG_SIZE];
        itoa(value, buffer, DECIMAL);
        MatterAwsSendMsg("lift/position set", (const char *) (buffer));
#endif
    }
}

// =============================================================================
// AppTask
// =============================================================================

AppTask::Cover & AppTask::GetCover()
{
    return mCoverList[mCurrentCover];
}

AppTask::Cover * AppTask::GetCover(chip::EndpointId endpoint)
{
    for (uint16_t i = 0; i < WINDOW_COVER_COUNT; ++i)
    {
        if (mCoverList[i].mEndpoint == endpoint)
        {
            return &mCoverList[i];
        }
    }
    return nullptr;
}

void AppTask::DispatchEventAttributeChange(chip::EndpointId endpoint, chip::AttributeId attribute)
{
    Cover * cover = GetCover(endpoint);
    chip::BitMask<Mode> mode;
    chip::BitMask<ConfigStatus> configStatus;
    chip::BitMask<OperationalStatus> opStatus;

    if (nullptr == cover)
    {
        ChipLogProgress(Zcl, "Ep[%u] not supported AttributeId=%u\n", endpoint, (unsigned int) attribute);
        return;
    }

    switch (attribute)
    {
    /* Changing the Target triggers motions on the real or simulated device */
    case Attributes::TargetPositionLiftPercent100ths::Id:
        cover->LiftGoToTarget();
        break;
    case Attributes::TargetPositionTiltPercent100ths::Id:
        cover->TiltGoToTarget();
        break;
    /* RO OperationalStatus */
    case Attributes::OperationalStatus::Id:
        chip::DeviceLayer::PlatformMgr().LockChipStack();
        opStatus = OperationalStatusGet(endpoint);
        OperationalStatusPrint(opStatus);
        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        UpdateLED();
        break;
    /* RW Mode */
    case Attributes::Mode::Id:
        chip::DeviceLayer::PlatformMgr().LockChipStack();
        mode = ModeGet(endpoint);
        ModePrint(mode);
        ModeSet(endpoint, mode);
        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        break;
    /* RO ConfigStatus */
    case Attributes::ConfigStatus::Id:
        chip::DeviceLayer::PlatformMgr().LockChipStack();
        configStatus = ConfigStatusGet(endpoint);
        ConfigStatusPrint(configStatus);
        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        break;
    /* Attributes changes ignored */
    case Attributes::Type::Id:
    case Attributes::EndProductType::Id:
    case Attributes::SafetyStatus::Id:
    case Attributes::CurrentPositionLiftPercent100ths::Id:
    case Attributes::CurrentPositionTiltPercent100ths::Id:
        UpdateLED();
#ifdef DISPLAY_ENABLED
        UpdateLCD();
#endif
        break;
    default:
        break;
    }
}

void AppTask::HandleLongPress()
{
    AppEvent event;
    event.Handler             = GeneralEventHandler;
    event.WindowEvent.Context = this;

    if (mUpPressed && mDownPressed)
    {
        mUpSuppressed = mDownSuppressed = true;
        mCurrentCover                   = mCurrentCover < WINDOW_COVER_COUNT - 1 ? mCurrentCover + 1 : 0;
        event.Type                      = AppEvent::kEventType_CoverChange;
        PostEvent(&event);
        ChipLogDetail(AppServer, "App controls set to cover %d", mCurrentCover + 1);
    }
    else if (mUpPressed)
    {
        mUpSuppressed = true;
        if (!mResetWarning)
        {
            event.Type = AppEvent::kEventType_ResetWarning;
            PostEvent(&event);
        }
    }
    else if (mDownPressed)
    {
        mDownSuppressed = true;
        Type type       = GetCover().CycleType();
        mTiltMode       = mTiltMode && (Type::kTiltBlindLiftAndTilt == type);
        ChipLogDetail(AppServer, "Cover type changed to %d", to_underlying(type));
    }
}

void AppTask::OnLongPressTimeout(AppTask::Timer & timer)
{
    AppTask * app = static_cast<AppTask *>(timer.mContext);
    if (app)
    {
        app->HandleLongPress();
    }
}

void AppTask::OnIconTimeout(AppTask::Timer & timer)
{
#ifdef DISPLAY_ENABLED
    GetAppTask().mIcon = LcdIcon::None;
    GetAppTask().UpdateLCD();
#endif
}

CHIP_ERROR AppTask::AppInit()
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    chip::DeviceLayer::Silabs::GetPlatform().SetButtonsCb(AppTask::ButtonEventHandler);

    chip::DeviceLayer::PlatformMgr().LockChipStack();

#ifdef DISPLAY_ENABLED
    mIconTimer = new Timer(LCD_ICON_TIMEOUT, OnIconTimeout, this);
#endif

    mLongPressTimer = new Timer(LONG_PRESS_TIMEOUT, OnLongPressTimeout, this);

    mCoverList[0].Init(WINDOW_COVER_ENDPOINT1);
    mCoverList[1].Init(WINDOW_COVER_ENDPOINT2);

    SuccessOrDie(
        SetTagList(WINDOW_COVER_ENDPOINT1,
                   chip::Span<const chip::app::Clusters::Descriptor::Structs::SemanticTagStruct::Type>(kEndpoint1TagList)));
    SuccessOrDie(
        SetTagList(WINDOW_COVER_ENDPOINT2,
                   chip::Span<const chip::app::Clusters::Descriptor::Structs::SemanticTagStruct::Type>(kEndpoint2TagList)));

    LEDWidget::InitGpio();
    mActionLED.Init(APP_ACTION_LED);
    LinkAppLed(&mActionLED);

    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    return err;
}

void AppTask::PostAttributeChange(chip::EndpointId endpoint, chip::AttributeId attributeId)
{
    AppEvent event     = CreateNewEvent(AppEvent::kEventType_AttributeChange);
    event.mEndpoint    = endpoint;
    event.mAttributeId = attributeId;
    PostEvent(&event);
}

void AppTask::UpdateLED()
{
    Cover & cover = GetCover();
    if (mResetWarning)
    {
        mActionLED.Set(false);
        mActionLED.Blink(500);
    }
    else
    {
        NPercent100ths current;
        LimitStatus liftLimit = LimitStatus::Intermediate;

        chip::DeviceLayer::PlatformMgr().LockChipStack();
        Attributes::CurrentPositionLiftPercent100ths::Get(cover.mEndpoint, current);
        chip::DeviceLayer::PlatformMgr().UnlockChipStack();

        if (!current.IsNull())
        {
            AbsoluteLimits limits = { .open = WC_PERCENT100THS_MIN_OPEN, .closed = WC_PERCENT100THS_MAX_CLOSED };
            liftLimit             = CheckLimitState(current.Value(), limits);
        }

        if (OperationalState::Stall != cover.mLiftOpState)
        {
            mActionLED.Blink(100);
        }
        else if (LimitStatus::IsUpOrOpen == liftLimit)
        {
            mActionLED.Set(true);
        }
        else if (LimitStatus::IsDownOrClose == liftLimit)
        {
            mActionLED.Set(false);
        }
        else
        {
            mActionLED.Blink(1000);
        }
    }
}

#ifdef DISPLAY_ENABLED
void AppTask::DrawUI(GLIB_Context_t * glibContext)
{
    GetAppTask().UpdateLCD();
}

void AppTask::UpdateLCD()
{
    if (BaseApplication::GetProvisionStatus())
    {
        Cover & cover = GetCover();
        chip::app::DataModel::Nullable<uint16_t> lift;
        chip::app::DataModel::Nullable<uint16_t> tilt;

        chip::DeviceLayer::PlatformMgr().LockChipStack();
        Type type = TypeGet(cover.mEndpoint);

        Attributes::CurrentPositionLift::Get(cover.mEndpoint, lift);
        Attributes::CurrentPositionTilt::Get(cover.mEndpoint, tilt);
        chip::DeviceLayer::PlatformMgr().UnlockChipStack();

        if (!tilt.IsNull() && !lift.IsNull())
        {
            LcdPainter::Paint(GetLCD(), type, lift.Value(), tilt.Value(), mIcon);
        }
    }
}
#endif

CHIP_ERROR AppTask::StartAppTask()
{
    return BaseApplication::StartAppTask(AppTaskMain);
}

void AppTask::AppTaskMain(void * pvParameter)
{
    AppEvent event;
    osMessageQueueId_t sAppEventQueue = *(static_cast<osMessageQueueId_t *>(pvParameter));

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

    GetAppTask().UpdateLED();
#ifdef DISPLAY_ENABLED
    GetAppTask().UpdateLCD();
#endif

    while (true)
    {
        osStatus_t eventReceived = osMessageQueueGet(sAppEventQueue, &event, NULL, osWaitForever);
        while (eventReceived == osOK)
        {
            GetAppTask().DispatchEvent(&event);
            eventReceived = osMessageQueueGet(sAppEventQueue, &event, NULL, 0);
        }
    }
}

void AppTask::ButtonEventHandler(uint8_t button, uint8_t btnAction)
{
    AppEvent event;

    if (btnAction == static_cast<uint8_t>(SilabsPlatform::ButtonAction::ButtonPressed))
    {
        event = CreateNewEvent(button ? AppEvent::kEventType_DownPressed : AppEvent::kEventType_UpPressed);
    }
    else
    {
        event = CreateNewEvent(button ? AppEvent::kEventType_DownReleased : AppEvent::kEventType_UpReleased);
    }

    AppTask::GetAppTask().PostEvent(&event);
}

void AppTask::GeneralEventHandler(AppEvent * aEvent)
{
    AppTask * app = static_cast<AppTask *>(aEvent->WindowEvent.Context);

    switch (aEvent->Type)
    {
    case AppEvent::kEventType_ResetWarning:
        app->mResetWarning = true;
        AppTask::GetAppTask().StartFactoryResetSequence();
        app->UpdateLED();
        break;

    case AppEvent::kEventType_ResetCanceled:
        app->mResetWarning = false;
        AppTask::GetAppTask().CancelFactoryResetSequence();
        app->UpdateLED();
        break;

    case AppEvent::kEventType_UpPressed:
        app->mUpPressed = true;
        if (app->mLongPressTimer)
        {
            app->mLongPressTimer->Start();
        }
        break;

    case AppEvent::kEventType_UpReleased:
        app->mUpPressed = false;
        if (app->mLongPressTimer)
        {
            app->mLongPressTimer->Stop();
        }
        if (app->mResetWarning)
        {
            aEvent->Type = AppEvent::kEventType_ResetCanceled;
            AppTask::GetAppTask().PostEvent(aEvent);
        }
        if (app->mUpSuppressed)
        {
            app->mUpSuppressed = false;
        }
        else if (app->mDownPressed)
        {
            app->mTiltMode       = !(app->mTiltMode);
            app->mDownSuppressed = true;
            aEvent->Type         = AppEvent::kEventType_TiltModeChange;
            AppTask::GetAppTask().PostEvent(aEvent);
        }
        else
        {
            app->GetCover().UpdateTargetPosition(OperationalState::MovingUpOrOpen,
                                                 ((app->mTiltMode) ? Cover::ControlAction::Tilt : Cover::ControlAction::Lift));
        }
        break;

    case AppEvent::kEventType_DownPressed:
        app->mDownPressed = true;
        if (app->mLongPressTimer)
        {
            app->mLongPressTimer->Start();
        }
        break;

    case AppEvent::kEventType_DownReleased:
        app->mDownPressed = false;
        if (app->mLongPressTimer)
        {
            app->mLongPressTimer->Stop();
        }
        if (app->mResetWarning)
        {
            aEvent->Type = AppEvent::kEventType_ResetCanceled;
            AppTask::GetAppTask().PostEvent(aEvent);
        }
        if (app->mDownSuppressed)
        {
            app->mDownSuppressed = false;
        }
        else if (app->mUpPressed)
        {
            app->mTiltMode     = !(app->mTiltMode);
            app->mUpSuppressed = true;
            aEvent->Type       = AppEvent::kEventType_TiltModeChange;
            AppTask::GetAppTask().PostEvent(aEvent);
        }
        else
        {
            app->GetCover().UpdateTargetPosition(
                OperationalState::MovingDownOrClose,
                ((app->mTiltMode) ? Cover::ControlAction::Tilt : Cover::ControlAction::Lift));
        }
        break;

    case AppEvent::kEventType_AttributeChange:
        app->DispatchEventAttributeChange(aEvent->mEndpoint, aEvent->mAttributeId);
        break;

#ifdef DISPLAY_ENABLED
    case AppEvent::kEventType_CoverTypeChange:
        app->UpdateLCD();
        break;
    case AppEvent::kEventType_CoverChange:
        if (app->mIconTimer != nullptr)
        {
            app->mIconTimer->Start();
        }
        app->mIcon = (app->GetCover().mEndpoint == 1) ? LcdIcon::One : LcdIcon::Two;
        app->UpdateLCD();
        break;
    case AppEvent::kEventType_TiltModeChange:
        ChipLogDetail(AppServer, "App control mode changed to %s", app->mTiltMode ? "Tilt" : "Lift");
        if (app->mIconTimer != nullptr)
        {
            app->mIconTimer->Start();
        }
        app->mIcon = app->mTiltMode ? LcdIcon::Tilt : LcdIcon::Lift;
        app->UpdateLCD();
        break;
#endif

    default:
        break;
    }
}

// =============================================================================
// DataModel Callbacks
// =============================================================================

void MatterPostAttributeChangeCallback(const app::ConcreteAttributePath & attributePath, uint8_t type, uint16_t size,
                                       uint8_t * value)
{
    AppTask::GetAppTask().DmCallbackMatterPostAttributeChangeCallback(attributePath, type, size, value);
}

void AppTask::DmCallbackMatterPostAttributeChangeCallback(const chip::app::ConcreteAttributePath & attributePath, uint8_t type,
                                                          uint16_t size, uint8_t * value)
{
    switch (attributePath.mClusterId)
    {
    case app::Clusters::Identify::Id:
        ChipLogProgress(Zcl, "Identify cluster ID: " ChipLogFormatMEI " Type: %u Value: %u, length %u",
                        ChipLogValueMEI(attributePath.mAttributeId), type, *value, size);
        break;
    default:
        break;
    }
}

void MatterWindowCoveringClusterServerAttributeChangedCallback(const app::ConcreteAttributePath & attributePath)
{
    AppTask::GetAppTask().DmCallbackMatterWindowCoveringClusterServerAttributeChangedCallback(attributePath);
}

void AppTask::DmCallbackMatterWindowCoveringClusterServerAttributeChangedCallback(
    const chip::app::ConcreteAttributePath & attributePath)
{
    PostAttributeChange(attributePath.mEndpointId, attributePath.mAttributeId);
}
