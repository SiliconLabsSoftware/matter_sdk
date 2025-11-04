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
#include "CookEndpoints.h"
#include "OvenBindingHandler.h"
#include "OvenEndpoint.h"

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app-common/zap-generated/cluster-objects.h>
#include <app/clusters/mode-base-server/mode-base-cluster-objects.h>
#include <platform/CHIPDeviceLayer.h>

#include "AppConfig.h"
#include "AppTask.h"
#include <platform/silabs/platformAbstraction/SilabsPlatform.h>

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
    VerifyOrReturn(mTemperatureControlledCabinetEndpoint.Init() == CHIP_NO_ERROR,
                   ChipLogError(AppServer, "TemperatureControlledCabinetEndpoint Init failed"));

    // Set initial state for TemperatureControlledCabinetEndpoint
    VerifyOrReturn(SetTemperatureControlledCabinetInitialState(kTemperatureControlledCabinetEndpoint) == CHIP_NO_ERROR,
                   ChipLogError(AppServer, "SetTemperatureControlledCabinetInitialState failed"));

    // Register the shared TemperatureLevelsDelegate for all the cooksurface endpoints
    TemperatureControl::SetInstance(&mTemperatureControlDelegate);

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

    // Get CookTop On/Off value
<<<<<<< HEAD
    bool currentState = false;
    chip::Protocols::InteractionModel::Status status = OnOffServer::Instance().getOnOffValue(kCookTopEndpoint, &currentState);
=======
    bool currentLedState                             = false;
    chip::Protocols::InteractionModel::Status status = OnOffServer::Instance().getOnOffValue(kCookTopEndpoint, &currentLedState);
>>>>>>> 3c958a2913 (Restyled by clang-format)
    VerifyOrReturn(status == Status::Success, ChipLogError(AppServer, "Failed to get CookTop OnOff value"));
    mCookTopState = currentState ? kCookTopState_On : kCookTopState_Off;

    status = OnOffServer::Instance().getOnOffValue(kCookSurfaceEndpoint1, &currentState);
    VerifyOrReturn(status == Status::Success, ChipLogError(AppServer, "Failed to get CookSurface OnOff value"));
    mCookSurfaceState1 = currentState ? kCookSurfaceState_On : kCookSurfaceState_Off;

    status = OnOffServer::Instance().getOnOffValue(kCookSurfaceEndpoint2, &currentState);
    VerifyOrReturn(status == Status::Success, ChipLogError(AppServer, "Failed to get CookSurface OnOff value"));
    mCookSurfaceState2 = currentState ? kCookSurfaceState_On : kCookSurfaceState_Off;

    // Get current oven mode
    status = OvenMode::Attributes::CurrentMode::Get(kTemperatureControlledCabinetEndpoint, &mCurrentOvenMode);
    VerifyOrReturn(status == Status::Success, ChipLogError(AppServer, "Unable to get the current oven mode"));

    DeviceLayer::PlatformMgr().UnlockChipStack();

    // Initialize binding manager (after stack unlock to avoid long hold)
    InitOvenBindingHandler();
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
    Action_t action = INVALID_ACTION;
    switch (endpointId)
    {
    case kCookTopEndpoint: {
        mCookTopState = (*value != 0) ? kCookTopState_On : kCookTopState_Off;
        // Turn on/off the associated cook surfaces.
        VerifyOrReturn(mCookSurfaceEndpoint1.SetOnOffState(*value) == Status::Success,
                       ChipLogError(AppServer, "Failed to set CookSurfaceEndpoint1 state"));
        VerifyOrReturn(mCookSurfaceEndpoint2.SetOnOffState(*value) == Status::Success,
                       ChipLogError(AppServer, "Failed to set CookSurfaceEndpoint2 state"));

        action = (*value != 0) ? COOK_TOP_ON_ACTION : COOK_TOP_OFF_ACTION;
        // Trigger binding for CookTop OnOff changes
        OnOffBindingContext * context = Platform::New<OnOffBindingContext>();
        if (context != nullptr)
        {
            context->localEndpointId = kCookTopEndpoint;
            context->commandId       = *value ? Clusters::OnOff::Commands::On::Id : Clusters::OnOff::Commands::Off::Id;

            if (CookTopOnOffBindingTrigger(context) != CHIP_NO_ERROR)
            {
                Platform::Delete(context);
                ChipLogError(AppServer, "Failed to schedule CookTopOnOffBindingTrigger, context freed");
            }
        }
        AppTask::GetAppTask().UpdateLED(*value);
        AppTask::GetAppTask().UpdateLCD();
        break;
    }
    case kCookSurfaceEndpoint1:
    case kCookSurfaceEndpoint2:
        if (endpointId == kCookSurfaceEndpoint1)
            mCookSurfaceState1 = (*value != 0) ? kCookSurfaceState_On : kCookSurfaceState_Off;
        else
            mCookSurfaceState2 = (*value != 0) ? kCookSurfaceState_On : kCookSurfaceState_Off;

        // Turn off CookTop if both the CookSurfaces are off.
        if (mCookSurfaceState1 == kCookSurfaceState_Off && mCookSurfaceState2 == kCookSurfaceState_Off)
        {
            VerifyOrReturn(mCookTopEndpoint.SetOnOffState(false) == Status::Success,
                           ChipLogError(AppServer, "Failed to set CookTopEndpoint state"));

            mCookTopState = kCookTopState_Off;
        }
        break;
    default:
        break;
    }

    AppEvent event         = {};
    event.Type             = AppEvent::kEventType_Oven;
    event.OvenEvent.Action = action;
    event.Handler          = OvenActionHandler;
    AppTask::GetAppTask().PostEvent(&event);
}

void OvenManager::OvenModeAttributeChangeHandler(chip::EndpointId endpointId, chip::AttributeId attributeId, uint8_t * value,
                                                 uint16_t size)
{
    mCurrentOvenMode       = *value;
    AppEvent event         = {};
    event.Type             = AppEvent::kEventType_Oven;
    event.OvenEvent.Action = OVEN_MODE_UPDATE_ACTION;
    event.Handler          = OvenActionHandler;
    AppTask::GetAppTask().PostEvent(&event);
    return;
}

void OvenManager::OvenActionHandler(AppEvent * aEvent)
{
    // TODO: Emulate hardware Action : Update the LEDs and LCD of oven-app as required.
}

bool OvenManager::IsTransitionBlocked(uint8_t fromMode, uint8_t toMode)
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
