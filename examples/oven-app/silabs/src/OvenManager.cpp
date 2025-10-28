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
#include <platform/CHIPDeviceLayer.h>

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

    DeviceLayer::PlatformMgr().UnlockChipStack();
}

CHIP_ERROR OvenManager::SetCookSurfaceInitialState(EndpointId cookSurfaceEndpoint)
{
    // Set the initial temperature-measurement values for CookSurfaceEndpoint
    Status status = TemperatureMeasurement::Attributes::MeasuredValue::Set(cookSurfaceEndpoint, 0);
    VerifyOrReturnError(status == Status::Success, CHIP_ERROR_INTERNAL,
                        ChipLogError(AppServer, "Setting MeasuredValue failed : %u", to_underlying(status)));

    // Initialize min/max measured values (range: 0 to 30000 -> 0.00C to 300.00C if unit is 0.01C) for cook surface endpoint
    status = TemperatureMeasurement::Attributes::MinMeasuredValue::Set(cookSurfaceEndpoint, 0);
    VerifyOrReturnError(status == Status::Success, CHIP_ERROR_INTERNAL,
                        ChipLogError(AppServer, "Setting MinMeasuredValue failed : %u", to_underlying(status)));
    status = TemperatureMeasurement::Attributes::MaxMeasuredValue::Set(cookSurfaceEndpoint, 30000);
    VerifyOrReturnError(status == Status::Success, CHIP_ERROR_INTERNAL,
                        ChipLogError(AppServer, "Setting MaxMeasuredValue failed : %u", to_underlying(status)));
    return CHIP_NO_ERROR;
}

CHIP_ERROR OvenManager::SetTemperatureControlledCabinetInitialState(EndpointId temperatureControlledCabinetEndpoint)
{
    Status tcStatus = TemperatureControl::Attributes::TemperatureSetpoint::Set(temperatureControlledCabinetEndpoint, 0);
    VerifyOrReturnError(tcStatus == Status::Success, CHIP_ERROR_INTERNAL,
                        ChipLogError(AppServer, "Endpoint2 TemperatureSetpoint init failed"));

    tcStatus = TemperatureControl::Attributes::MinTemperature::Set(temperatureControlledCabinetEndpoint, 0);
    VerifyOrReturnError(tcStatus == Status::Success, CHIP_ERROR_INTERNAL,
                        ChipLogError(AppServer, "Endpoint2 MinTemperature init failed"));

    tcStatus = TemperatureControl::Attributes::MaxTemperature::Set(temperatureControlledCabinetEndpoint, 30000);
    VerifyOrReturnError(tcStatus == Status::Success, CHIP_ERROR_INTERNAL,
                        ChipLogError(AppServer, "Endpoint2 MaxTemperature init failed"));

    tcStatus = TemperatureControl::Attributes::Step::Set(temperatureControlledCabinetEndpoint, 500);
    VerifyOrReturnError(tcStatus == Status::Success, CHIP_ERROR_INTERNAL, ChipLogError(AppServer, "Endpoint2 Step init failed"));

    return CHIP_NO_ERROR;
}
