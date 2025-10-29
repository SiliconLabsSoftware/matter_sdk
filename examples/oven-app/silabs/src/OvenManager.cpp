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
#include "CookSurfaceEndpoint.h"
#include "CookTopEndpoint.h"
#include "OvenEndpoint.h"

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app-common/zap-generated/cluster-objects.h>
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

    // Initialize states
    mCookTopState = kCookTopState_OffCompleted;
    mCookSurfaceState1 = kCookSurfaceState_OffCompleted;
    mCookSurfaceState2 = kCookSurfaceState_OffCompleted;

    // Endpoint initializations
    VerifyOrReturn(mOvenEndpoint.Init() == CHIP_NO_ERROR, ChipLogError(AppServer, "OvenEndpoint Init failed"));

    VerifyOrReturn(mTemperatureControlledCabinetEndpoint.Init() == CHIP_NO_ERROR,
                   ChipLogError(AppServer, "TemperatureControlledCabinetEndpoint Init failed"));

    // Set initial state for TemperatureControlledCabinetEndpoint
    VerifyOrReturn(SetTemperatureControlledCabinetInitialState(kTemperatureControlledCabinetEndpoint) == CHIP_NO_ERROR,
                   ChipLogError(AppServer, "SetTemperatureControlledCabinetInitialState failed"));

    VerifyOrReturn(mCookTopEndpoint.Init() == CHIP_NO_ERROR, ChipLogError(AppServer, "CookTopEndpoint Init failed"));

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
    switch (endpointId)
    {
    case kTemperatureControlledCabinetEndpoint:
        // TODO: Update the LCD with the new Temperature Control attribute value
        break;
    default:
        break;
    }
}

void OvenManager::OnOffAttributeChangeHandler(EndpointId endpointId, AttributeId attributeId, uint8_t * value, uint16_t size)
{
    switch (endpointId)
    {
    case kCookTopEndpoint:
        InitiateAction(AppEvent::kEventType_CookTop, *value ? OvenManager::ON_ACTION : OvenManager::OFF_ACTION, value);
        // Update CookSurface states accordingly
        mCookSurfaceEndpoint1.SetOnOffState(*value);
        mCookSurfaceEndpoint2.SetOnOffState(*value);
        break;
    case kCookSurfaceEndpoint1:
    case kCookSurfaceEndpoint2:
        // Handle On/Off attribute changes for the cook surface endpoints
        InitiateCookSurfaceAction(AppEvent::kEventType_CookSurface, *value ? OvenManager::ON_ACTION : OvenManager::OFF_ACTION, value, endpointId);
        {
            bool cookSurfaceEndpoint1State;
            bool cookSurfaceEndpoint2State;
            mCookSurfaceEndpoint1.GetOnOffState(cookSurfaceEndpoint1State);
            mCookSurfaceEndpoint2.GetOnOffState(cookSurfaceEndpoint2State);
            // Check if both cooksurfaces are off. If yes, turn off the cooktop (call cooktop.TurnOffCookTop)
            if (cookSurfaceEndpoint1State == false  && cookSurfaceEndpoint2State == false)
            {
                mCookTopEndpoint.SetOnOffState(false);
            }
        }
        break;
    default:
        break;
    }
}

void OvenManager::OvenModeAttributeChangeHandler(chip::EndpointId endpointId, chip::AttributeId attributeId, uint8_t * value,
                                                 uint16_t size)
{
    VerifyOrReturn(endpointId == kTemperatureControlledCabinetEndpoint,
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
    if (mCookTopState == kCookTopState_OffCompleted && aAction == ON_ACTION)
    {
        action_initiated = true;
        new_state        = kCookTopState_OnInitiated;
    }
    else if (mCookTopState == kCookTopState_OnCompleted && aAction == OFF_ACTION)
    {
        action_initiated = true;
        new_state        = kCookTopState_OffInitiated;
    }

    if (action_initiated && (aAction == ON_ACTION || aAction == OFF_ACTION))
    {
        mCookTopState = new_state;

        AppEvent event;
        event.Type              = AppEvent::kEventType_CookTop;
        event.OvenEvent.Context = this;
        event.Handler           = ActuatorMovementHandler;
        AppTask::GetAppTask().PostEvent(&event);
    }

    if (action_initiated && mActionInitiated_CB)
    {
        mActionInitiated_CB(aAction, aActor, aValue);
    }

    return action_initiated;
}

bool OvenManager::InitiateCookSurfaceAction(int32_t aActor, Action_t aAction, uint8_t * aValue, chip::EndpointId endpointId)
{
    bool action_initiated = false;
    State_t new_state;
    State_t * currentState = nullptr;

    // Get the appropriate state pointer based on endpoint
    if (endpointId == kCookSurfaceEndpoint1)
    {
        currentState = &mCookSurfaceState1;
    }
    else if (endpointId == kCookSurfaceEndpoint2)
    {
        currentState = &mCookSurfaceState2;
    }
    else
    {
        return false; // Invalid endpoint
    }

    // Initiate Turn On/Off Action only when the previous one is complete.
    if (*currentState == kCookSurfaceState_OffCompleted && aAction == ON_ACTION)
    {
        action_initiated = true;
        new_state        = kCookSurfaceState_OnInitiated;
    }
    else if (*currentState == kCookSurfaceState_OnCompleted && aAction == OFF_ACTION)
    {
        action_initiated = true;
        new_state        = kCookSurfaceState_OffInitiated;
    }

    if (action_initiated && (aAction == ON_ACTION || aAction == OFF_ACTION))
    {
        *currentState = new_state;

        AppEvent event;
        event.Type                      = AppEvent::kEventType_CookSurface;
        event.OvenEvent.Context  = this;
        event.OvenEvent.Action   = aAction;
        event.OvenEvent.Actor    = endpointId; // Store endpoint ID in Actor field
        event.Handler                   = ActuatorMovementHandler;
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
    OvenManager * oven = static_cast<OvenManager *>(aEvent->OvenEvent.Context);

    switch (aEvent->Type)
    {
    case AppEvent::kEventType_CookTop:
        {
            // Handle CookTop state transitions
            if (oven->mCookTopState == kCookTopState_OffInitiated)
            {
                oven->mCookTopState    = kCookTopState_OffCompleted;
                actionCompleted = OFF_ACTION;
            }
            else if (oven->mCookTopState == kCookTopState_OnInitiated)
            {
                oven->mCookTopState    = kCookTopState_OnCompleted;
                actionCompleted = ON_ACTION;
            }
        }
        break;
    case AppEvent::kEventType_CookSurface:
        {
            // Handle CookSurface state transitions
            chip::EndpointId endpointId = static_cast<chip::EndpointId>(aEvent->OvenEvent.Actor);
            State_t * currentState = nullptr;

            // Get the appropriate state pointer based on endpoint
            if (endpointId == kCookSurfaceEndpoint1)
            {
                currentState = &oven->mCookSurfaceState1;
            }
            else if (endpointId == kCookSurfaceEndpoint2)
            {
                currentState = &oven->mCookSurfaceState2;
            }
            else
            {
                ChipLogError(AppServer, "Invalid CookSurface endpoint ID");
                return; // Invalid endpoint
            }

            if (*currentState == kCookSurfaceState_OffInitiated)
            {
                *currentState   = kCookSurfaceState_OffCompleted;
                actionCompleted = OFF_ACTION;
            }
            else if (*currentState == kCookSurfaceState_OnInitiated)
            {
                *currentState   = kCookSurfaceState_OnCompleted;
                actionCompleted = ON_ACTION;
            }
        }
        break;
    default:
        break;
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
    { to_underlying(TemperatureControlledCabinet::OvenModeDelegate::OvenModes::kModeGrill), to_underlying(TemperatureControlledCabinet::OvenModeDelegate::OvenModes::kModeProofing) },
    { to_underlying(TemperatureControlledCabinet::OvenModeDelegate::OvenModes::kModeProofing), to_underlying(TemperatureControlledCabinet::OvenModeDelegate::OvenModes::kModeClean) },
    { to_underlying(TemperatureControlledCabinet::OvenModeDelegate::OvenModes::kModeClean), to_underlying(TemperatureControlledCabinet::OvenModeDelegate::OvenModes::kModeBake) },
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

    // Verify newMode is among supported modes
    bool supported = mTemperatureControlledCabinetEndpoint.GetOvenModeDelegate().IsSupportedMode(newMode);
    if (!supported)
    {
        response.status = to_underlying(ModeBase::StatusCode::kUnsupportedMode);
        return;
    }

    // Read Current Oven Mode
    uint8_t currentMode;
    Status attrStatus = OvenMode::Attributes::CurrentMode::Get(endpointId, &currentMode);
    if (attrStatus != Status::Success)
    {
        ChipLogError(AppServer, "OvenManager: Failed to read CurrentMode");
        response.status = to_underlying(ModeBase::StatusCode::kGenericFailure);
        response.statusText.SetValue(CharSpan::fromCharString("Read CurrentMode failed"));
        return;
    }

    // No action needed if current mode is the same as new mode
    if (currentMode == newMode)
    {
        response.status = to_underlying(ModeBase::StatusCode::kSuccess);
        return;
    }

    // Check if the mode transition is possible
    if (IsTransitionBlocked(currentMode, newMode))
    {
        ChipLogProgress(AppServer, "OvenManager: Blocked transition %u -> %u", currentMode, newMode);
        response.status = to_underlying(ModeBase::StatusCode::kGenericFailure);
        response.statusText.SetValue(CharSpan::fromCharString("Transition blocked"));
        return;
    }

    // Write new mode
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
