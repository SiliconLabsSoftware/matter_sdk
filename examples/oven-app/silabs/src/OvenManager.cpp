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
    VerifyOrReturn(mOvenEndpoint1.Init() == CHIP_NO_ERROR, ChipLogError(AppServer, "OvenEndpoint1 Init failed"));

    VerifyOrReturn(mTemperatureControlledCabinetEndpoint2.Init() == CHIP_NO_ERROR,
                   ChipLogError(AppServer, "TemperatureControlledCabinetEndpoint2 Init failed"));

    VerifyOrReturn(mCookTopEndpoint3.Init() == CHIP_NO_ERROR, ChipLogError(AppServer, "CookTopEndpoint3 Init failed"));

    // Register the shared TemperatureLevelsDelegate for all the cooksurface endpoints
    TemperatureControl::SetInstance(&mTemperatureControlDelegate);
    VerifyOrReturn(mCookSurfaceEndpoint4.Init() == CHIP_NO_ERROR, ChipLogError(AppServer, "CookSurfaceEndpoint4 Init failed"));
    VerifyOrReturn(mCookSurfaceEndpoint5.Init() == CHIP_NO_ERROR, ChipLogError(AppServer, "CookSurfaceEndpoint5 Init failed"));

    // Set the initial temperature-measurement values for CookSurfaceEndpoint4 and CookSurfaceEndpoint5
    Status status = TemperatureMeasurement::Attributes::MeasuredValue::Set(kCookSurfaceEndpoint4, 0);
    VerifyOrReturn(status == Status::Success, ChipLogError(AppServer, "CookSurfaceEndpoint4 MeasuredValue init failed"));
    status = TemperatureMeasurement::Attributes::MeasuredValue::Set(kCookSurfaceEndpoint5, 0);
    VerifyOrReturn(status == Status::Success, ChipLogError(AppServer, "CookSurfaceEndpoint5 MeasuredValue init failed"));

    // Initialize min/max measured values (range: 0 to 30000 -> 0.00C to 300.00C if unit is 0.01C) for both cook surface endpoints
    status = TemperatureMeasurement::Attributes::MinMeasuredValue::Set(kCookSurfaceEndpoint4, 0);
    VerifyOrReturn(status == Status::Success, ChipLogError(AppServer, "CookSurfaceEndpoint4 MinMeasuredValue init failed"));
    status = TemperatureMeasurement::Attributes::MaxMeasuredValue::Set(kCookSurfaceEndpoint4, 30000);
    VerifyOrReturn(status == Status::Success, ChipLogError(AppServer, "CookSurfaceEndpoint4 MaxMeasuredValue init failed"));

    status = TemperatureMeasurement::Attributes::MinMeasuredValue::Set(kCookSurfaceEndpoint5, 0);
    VerifyOrReturn(status == Status::Success, ChipLogError(AppServer, "CookSurfaceEndpoint5 MinMeasuredValue init failed"));
    status = TemperatureMeasurement::Attributes::MaxMeasuredValue::Set(kCookSurfaceEndpoint5, 30000);
    VerifyOrReturn(status == Status::Success, ChipLogError(AppServer, "CookSurfaceEndpoint5 MaxMeasuredValue init failed"));

    // Register supported temperature levels (Low, Medium, High) for CookSurface endpoints 4 and 5
    static const CharSpan kCookSurfaceLevels[] = { CharSpan::fromCharString("Low"), CharSpan::fromCharString("Medium"),
                                                   CharSpan::fromCharString("High") };
    bool err                                   = mTemperatureControlDelegate.RegisterSupportedLevels(
        kCookSurfaceEndpoint4, kCookSurfaceLevels,
        static_cast<uint8_t>(AppSupportedTemperatureLevelsDelegate::kNumTemperatureLevels));

    VerifyOrReturn(err, ChipLogError(AppServer, "RegisterSupportedLevels failed for endpoint 4"));

    err = mTemperatureControlDelegate.RegisterSupportedLevels(
        kCookSurfaceEndpoint5, kCookSurfaceLevels,
        static_cast<uint8_t>(AppSupportedTemperatureLevelsDelegate::kNumTemperatureLevels));

    VerifyOrReturn(err, ChipLogError(AppServer, "RegisterSupportedLevels failed for endpoint 5"));

    DeviceLayer::PlatformMgr().UnlockChipStack();
}
