/*
 *
 *    Copyright (c) 2025 Project CHIP Authors
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

#include "OvenManager.h"
// Corrected relative paths to shared oven-app-common headers (src/ -> ../../)
#include "CookSurfaceEndpoint.h"
#include "CookTopEndpoint.h"
#include "OvenEndpoint.h"

#include <app-common/zap-generated/cluster-objects.h>
#include <app-common/zap-generated/attributes/Accessors.h>
#include <app/clusters/mode-base-server/mode-base-cluster-objects.h>

#include "AppConfig.h"
#include "AppTask.h"

#include <app-common/zap-generated/attributes/Accessors.h>
#include <platform/CHIPDeviceLayer.h>

#define MAX_TEMPERATURE 30000
#define MIN_TEMPERATURE 0
#define TEMPERATURE_STEP 500

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using chip::Protocols::InteractionModel::Status;

OvenManager OvenManager::sOvenMgr;

void OvenManager::Init()
{
    DeviceLayer::PlatformMgr().LockChipStack();
    // Endpoint initializations
    VerifyOrReturn(mOvenEndpoint.Init() == CHIP_NO_ERROR, ChipLogError(AppServer, "OvenEndpoint Init failed"));

    VerifyOrReturn(mTemperatureControlledCabinetEndpoint.Init() == CHIP_NO_ERROR,
                   ChipLogError(AppServer, "TemperatureControlledCabinetEndpoint Init failed"));

    // Set initial state for TemperatureControlledCabinetEndpoint
    VerifyOrReturn(SetTemperatureControlledCabinetInitialState(kTemperatureControlledCabinetEndpoint) == CHIP_NO_ERROR,
                   ChipLogError(AppServer, "SetTemperatureControlledCabinetInitialState failed"));

    VerifyOrReturn(mCookTopEndpoint.Init() == CHIP_NO_ERROR, ChipLogError(AppServer, "CookTopEndpoint Init failed"));

    // Initialize TemperatureControl cluster numeric temperature attributes for endpoint 2 (silent on failure)
    {
    Status tcStatus = TemperatureControl::Attributes::TemperatureSetpoint::Set(kTemperatureControlledCabinetEndpoint2, 0);
    VerifyOrReturn(tcStatus == Status::Success,
               ChipLogError(AppServer, "Endpoint2 TemperatureSetpoint init failed"));

    tcStatus = TemperatureControl::Attributes::MinTemperature::Set(kTemperatureControlledCabinetEndpoint2, 0);
    VerifyOrReturn(tcStatus == Status::Success,
               ChipLogError(AppServer, "Endpoint2 MinTemperature init failed"));

    tcStatus = TemperatureControl::Attributes::MaxTemperature::Set(kTemperatureControlledCabinetEndpoint2, 30000);
    VerifyOrReturn(tcStatus == Status::Success,
               ChipLogError(AppServer, "Endpoint2 MaxTemperature init failed"));

    tcStatus = TemperatureControl::Attributes::Step::Set(kTemperatureControlledCabinetEndpoint2, 500);
    VerifyOrReturn(tcStatus == Status::Success, ChipLogError(AppServer, "Endpoint2 Step init failed"));
    }

    // Register the shared TemperatureLevelsDelegate for all the cooksurface endpoints
    TemperatureControl::SetInstance(&mTemperatureControlDelegate);

    VerifyOrReturn(mCookSurfaceEndpoint1.Init() == CHIP_NO_ERROR, ChipLogError(AppServer, "CookSurfaceEndpoint1 Init failed"));
    VerifyOrReturn(mCookSurfaceEndpoint2.Init() == CHIP_NO_ERROR, ChipLogError(AppServer, "CookSurfaceEndpoint2 Init failed"));

    // Set initial state for CookSurface endpoints
    VerifyOrReturn(SetCookSurfaceInitialState(mCookSurfaceEndpoint1.GetEndpointId()) == CHIP_NO_ERROR,
                   ChipLogError(AppServer, "SetCookSurfaceInitialState failed for CookSurfaceEndpoint1"));
    VerifyOrReturn(SetCookSurfaceInitialState(mCookSurfaceEndpoint2.GetEndpointId()) == CHIP_NO_ERROR,
                   ChipLogError(AppServer, "SetCookSurfaceInitialState failed for CookSurfaceEndpoint2"));

    // Register supported temperature levels (Low, Medium, High) for CookSurface endpoints 1 and 2
    static const CharSpan kCookSurfaceLevels[] = { CharSpan::fromCharString("Low"), CharSpan::fromCharString("Medium"),
                                                   CharSpan::fromCharString("High") };
    CHIP_ERROR err                             = mTemperatureControlDelegate.RegisterSupportedLevels(
        kCookSurfaceEndpoint1, kCookSurfaceLevels,
        static_cast<uint8_t>(AppSupportedTemperatureLevelsDelegate::kNumTemperatureLevels));

    VerifyOrReturn(err == CHIP_NO_ERROR, ChipLogError(AppServer, "RegisterSupportedLevels failed for CookSurfaceEndpoint1"));

    err = mTemperatureControlDelegate.RegisterSupportedLevels(
        kCookSurfaceEndpoint2, kCookSurfaceLevels,
        static_cast<uint8_t>(AppSupportedTemperatureLevelsDelegate::kNumTemperatureLevels));

    VerifyOrReturn(err == CHIP_NO_ERROR, ChipLogError(AppServer, "RegisterSupportedLevels failed for CookSurfaceEndpoint2"));

    VerifyOrReturn(mCookSurfaceEndpoint1.Init() == CHIP_NO_ERROR, ChipLogError(AppServer, "CookSurfaceEndpoint1 Init failed"));
    VerifyOrReturn(mCookSurfaceEndpoint2.Init() == CHIP_NO_ERROR, ChipLogError(AppServer, "CookSurfaceEndpoint2 Init failed"));

    // Set initial state for CookSurface endpoints
    VerifyOrReturn(SetCookSurfaceInitialState(mCookSurfaceEndpoint1.GetEndpointId()) == CHIP_NO_ERROR,
                   ChipLogError(AppServer, "SetCookSurfaceInitialState failed for CookSurfaceEndpoint1"));
    VerifyOrReturn(SetCookSurfaceInitialState(mCookSurfaceEndpoint2.GetEndpointId()) == CHIP_NO_ERROR,
                   ChipLogError(AppServer, "SetCookSurfaceInitialState failed for CookSurfaceEndpoint2"));

    // Register supported temperature levels (Low, Medium, High) for CookSurface endpoints 1 and 2
    static const CharSpan kCookSurfaceLevels[] = { CharSpan::fromCharString("Low"), CharSpan::fromCharString("Medium"),
                                                   CharSpan::fromCharString("High") };
    CHIP_ERROR err                             = mTemperatureControlDelegate.RegisterSupportedLevels(
        kCookSurfaceEndpoint1, kCookSurfaceLevels,
        static_cast<uint8_t>(AppSupportedTemperatureLevelsDelegate::kNumTemperatureLevels));

    VerifyOrReturn(err == CHIP_NO_ERROR, ChipLogError(AppServer, "RegisterSupportedLevels failed for CookSurfaceEndpoint1"));

    err = mTemperatureControlDelegate.RegisterSupportedLevels(
        kCookSurfaceEndpoint2, kCookSurfaceLevels,
        static_cast<uint8_t>(AppSupportedTemperatureLevelsDelegate::kNumTemperatureLevels));

    VerifyOrReturn(err == CHIP_NO_ERROR, ChipLogError(AppServer, "RegisterSupportedLevels failed for CookSurfaceEndpoint2"));

    DeviceLayer::PlatformMgr().UnlockChipStack();
}

CHIP_ERROR OvenManager::SetCookSurfaceInitialState(EndpointId cookSurfaceEndpoint)
{
    // Set the initial temperature-measurement values for CookSurfaceEndpoint
    Status status = TemperatureMeasurement::Attributes::MeasuredValue::Set(cookSurfaceEndpoint, MIN_TEMPERATURE);
    VerifyOrReturnError(status == Status::Success, CHIP_ERROR_INTERNAL,
                        ChipLogError(AppServer, "Setting MeasuredValue failed : %u", to_underlying(status)));

    // Initialize min/max measured values (range: 0 to 30000 -> 0.00C to 300.00C if unit is 0.01C) for cook surface endpoint
    status = TemperatureMeasurement::Attributes::MinMeasuredValue::Set(cookSurfaceEndpoint, MIN_TEMPERATURE);
    VerifyOrReturnError(status == Status::Success, CHIP_ERROR_INTERNAL,
                        ChipLogError(AppServer, "Setting MinMeasuredValue failed : %u", to_underlying(status)));
    status = TemperatureMeasurement::Attributes::MaxMeasuredValue::Set(cookSurfaceEndpoint, MAX_TEMPERATURE);
    VerifyOrReturnError(status == Status::Success, CHIP_ERROR_INTERNAL,
                        ChipLogError(AppServer, "Setting MaxMeasuredValue failed : %u", to_underlying(status)));
    return CHIP_NO_ERROR;
}

CHIP_ERROR OvenManager::SetTemperatureControlledCabinetInitialState(EndpointId temperatureControlledCabinetEndpoint)
{
    Status tcStatus =
        TemperatureControl::Attributes::TemperatureSetpoint::Set(temperatureControlledCabinetEndpoint, MIN_TEMPERATURE);
    VerifyOrReturnError(tcStatus == Status::Success, CHIP_ERROR_INTERNAL,
                        ChipLogError(AppServer, "Setting TemperatureSetpoint failed : %u", to_underlying(tcStatus)));

    tcStatus = TemperatureControl::Attributes::MinTemperature::Set(temperatureControlledCabinetEndpoint, MIN_TEMPERATURE);
    VerifyOrReturnError(tcStatus == Status::Success, CHIP_ERROR_INTERNAL,
                        ChipLogError(AppServer, "Setting MinTemperature failed : %u", to_underlying(tcStatus)));

    tcStatus = TemperatureControl::Attributes::MaxTemperature::Set(temperatureControlledCabinetEndpoint, MAX_TEMPERATURE);
    VerifyOrReturnError(tcStatus == Status::Success, CHIP_ERROR_INTERNAL,
                        ChipLogError(AppServer, "Setting MaxTemperature failed : %u", to_underlying(tcStatus)));

    tcStatus = TemperatureControl::Attributes::Step::Set(temperatureControlledCabinetEndpoint, TEMPERATURE_STEP);
    VerifyOrReturnError(tcStatus == Status::Success, CHIP_ERROR_INTERNAL,
                        ChipLogError(AppServer, "Setting Step failed : %u", to_underlying(tcStatus)));

    return CHIP_NO_ERROR;
}

void OvenManager::TempCtrlAttributeChangeHandler(EndpointId endpointId, AttributeId attributeId, uint8_t * value, uint16_t size)
{
    if (endpointId == kCookSurfaceEndpoint4 || endpointId == kCookSurfaceEndpoint5)
    {
        // Handle temperature control attribute changes for the cook surface endpoints
        if (*value == 0) // low
        {
            // TODO: Adjust the temperature-setpoint value in TemperatureControl Cluster accordingly
        }
        else if (*value == 1) // medium
        {
            // TODO: Adjust the temperature-setpoint value in TemperatureControl Cluster accordingly
        }
        else if (*value == 2) // high
        {
            // TODO: Adjust the temperature-setpoint value in TemperatureControl Cluster accordingly
        }
    }
    return;
}

void OvenManager::OnOffAttributeChangeHandler(EndpointId endpointId, AttributeId attributeId, uint8_t * value, uint16_t size)
{
    if (endpointId == kCookTopEndpoint3)
    {
        // Handle LCD and LED Actions
        InitiateAction(AppEvent::kEventType_Oven, *value ? OvenManager::ON_ACTION : OvenManager::OFF_ACTION, value);
        
        // Update CookSurface states accordingly
        mCookSurfaceEndpoint4.SetOnOffState(*value);
        mCookSurfaceEndpoint5.SetOnOffState(*value);
    }
    else if (endpointId == kCookSurfaceEndpoint4 || endpointId == kCookSurfaceEndpoint5)
    {
        // Handle On/Off attribute changes for the cook surface endpoints
        bool cookSurfaceEndpoint4State = mCookSurfaceEndpoint4.GetOnOffState();
        bool cookSurfaceEndpoint5State = mCookSurfaceEndpoint5.GetOnOffState();
        // Check if both cooksurfaces are off. If yes, turn off the cooktop (call cooktop.TurnOffCookTop)
        if (cookSurfaceEndpoint4State == false && cookSurfaceEndpoint5State == false)
        {
            mCookTopEndpoint3.SetOnOffState(false);
        }
    }
    return;
}

void OvenManager::OvenModeAttributeChangeHandler(chip::EndpointId endpointId, chip::AttributeId attributeId, uint8_t * value, uint16_t size)
{
    VerifyOrReturn(endpointId == kTemperatureControlledCabinetEndpoint2, 
                    ChipLogError(AppServer, "Command received over Unsupported Endpoint"));
    // TODO: Update the LCD with the new Oven Mode
    return;
}

void OvenManager::SetCallbacks(Callback_fn_initiated aActionInitiated_CB, Callback_fn_completed aActionCompleted_CB)
{
    mActionInitiated_CB = aActionInitiated_CB;
    mActionCompleted_CB = aActionCompleted_CB;
}

bool OvenManager::InitiateAction(int32_t aActor, Action_t aAction, uint8_t * aValue)
{
    bool action_initiated = false;
    State_t new_state;

    // Initiate Turn On/Off Action only when the previous one is complete.
    // if (((mState == kState_OffCompleted) || mOffEffectArmed) && aAction == ON_ACTION)
    if (mState == kState_OffCompleted && aAction == ON_ACTION)
    {
        action_initiated = true;

        new_state = kState_OnInitiated;
    }
    else if (mState == kState_OnCompleted && aAction == OFF_ACTION)
    {
        action_initiated = true;

        new_state = kState_OffInitiated;
    }

    if (action_initiated && (aAction == ON_ACTION || aAction == OFF_ACTION))
    {
        mState = new_state;

        AppEvent event;
        event.Type               = AppEvent::kEventType_Oven; // Assuming a distinct action type exists; fallback to Timer if not.
        event.OvenEvent.Context = this; // Reuse Context field; adjust if a specific union member exists for action.
        event.Handler            = ActuatorMovementHandler;
        AppTask::GetAppTask().PostEvent(&event);
    }

    if (action_initiated && mActionInitiated_CB)
    {
        mActionInitiated_CB(aAction, aActor, aValue);
    }

    return action_initiated;
}

void OvenManager::ActuatorMovementHandler(AppEvent * aEvent)
{
    Action_t actionCompleted = INVALID_ACTION;

    // Correct union member: event posted stored context in OvenEvent.Context, not TimerEvent.Context.
    OvenManager * oven = static_cast<OvenManager *>(aEvent->OvenEvent.Context);

    if (oven->mState == kState_OffInitiated)
    {
        oven->mState   = kState_OffCompleted;
        actionCompleted = OFF_ACTION;
    }
    else if (oven->mState == kState_OnInitiated)
    {
        oven->mState   = kState_OnCompleted;
        actionCompleted = ON_ACTION;
    }

    if (actionCompleted != INVALID_ACTION)
    {
        if (oven->mActionCompleted_CB)
        {
            oven->mActionCompleted_CB(actionCompleted);
        }
    }
}

// ---------------- Oven Mode Handling ----------------

namespace {
struct BlockedTransition
{
    uint8_t fromMode;
    uint8_t toMode;
};

// Disallowed OvenMode Transitions.
static constexpr BlockedTransition kBlockedTransitions[] = {
    { TemperatureControlledCabinet::OvenModeDelegate::kModeGrill, TemperatureControlledCabinet::OvenModeDelegate::kModeProofing },
    { TemperatureControlledCabinet::OvenModeDelegate::kModeProofing, TemperatureControlledCabinet::OvenModeDelegate::kModeClean },
    { TemperatureControlledCabinet::OvenModeDelegate::kModeClean, TemperatureControlledCabinet::OvenModeDelegate::kModeBake },
};

static bool IsTransitionBlocked(uint8_t fromMode, uint8_t toMode)
{
    for (auto const & bt : kBlockedTransitions)
    {
        if (bt.fromMode == fromMode && bt.toMode == toMode)
        {
            return true;
        }
    }
    return false;
}
} // namespace

void OvenManager::ProcessOvenModeChange(chip::EndpointId endpointId, uint8_t newMode,
                                        chip::app::Clusters::ModeBase::Commands::ChangeToModeResponse::Type & response)
{
    using namespace chip::app::Clusters;
    using chip::Protocols::InteractionModel::Status;
    ChipLogProgress(AppServer, "OvenManager::ProcessOvenModeChange ep=%u newMode=%u", endpointId, newMode);

    // 1. Verify newMode is among supported modes
    bool supported = TemperatureControlledCabinet::OvenModeDelegate::IsSupportedMode(newMode);
    if (!supported)
    {
        response.status = to_underlying(ModeBase::StatusCode::kUnsupportedMode);
        return;
    }

    // 2. Read current mode
    uint8_t currentMode;
    Status attrStatus   = OvenMode::Attributes::CurrentMode::Get(endpointId, &currentMode);
    if (attrStatus != Status::Success)
    {
    ChipLogError(AppServer, "OvenManager: Failed to read CurrentMode");
        response.status = to_underlying(ModeBase::StatusCode::kGenericFailure);
        response.statusText.SetValue(CharSpan::fromCharString("Read CurrentMode failed"));
        return;
    }

    // 3. No-op
    if (currentMode == newMode)
    {
        response.status = to_underlying(ModeBase::StatusCode::kSuccess);
        return;
    }

    // 4. Policy check
    if (IsTransitionBlocked(currentMode, newMode))
    {
        ChipLogProgress(AppServer, "OvenManager: Blocked transition %u -> %u", currentMode, newMode);
        response.status = to_underlying(ModeBase::StatusCode::kGenericFailure);
        response.statusText.SetValue(CharSpan::fromCharString("Transition blocked"));
        return;
    }

    // 5. Write new mode
    Status writeStatus = OvenMode::Attributes::CurrentMode::Set(endpointId, newMode);
    if (writeStatus != Status::Success)
    {
    ChipLogError(AppServer, "OvenManager: Failed to write CurrentMode");
        response.status = to_underlying(ModeBase::StatusCode::kGenericFailure);
        response.statusText.SetValue(CharSpan::fromCharString("Write CurrentMode failed"));
        return;
    }
    response.status = to_underlying(ModeBase::StatusCode::kSuccess);
}
