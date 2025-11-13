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

#include "ExtractorHoodEndpoint.h"

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app/clusters/fan-control-server/fan-control-server.h>
#include <lib/core/CHIPError.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>

#include <algorithm>

using namespace chip;
using namespace chip::app;
using namespace chip::DeviceLayer;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::FanControl;
using Status = chip::Protocols::InteractionModel::Status;

CHIP_ERROR ExtractorHoodEndpoint::Init()
{
    // Initialize percent current from percent setting
    // This ensures the fan speed reflects the current setting on startup
    DeviceLayer::PlatformMgr().LockChipStack();
    DataModel::Nullable<chip::Percent> percentSettingNullable = GetPercentSetting();
    DeviceLayer::PlatformMgr().UnlockChipStack();

    Percent initialPercentSetting = percentSettingNullable.IsNull() ? 0 : percentSettingNullable.Value();
    Status status                 = HandlePercentSettingChange(initialPercentSetting);
    if (status != Status::Success)
    {
        ChipLogError(NotSpecified, "ExtractorHoodEndpoint::Init: Failed to initialize PercentCurrent");
        return CHIP_ERROR_INTERNAL;
    }

    return CHIP_NO_ERROR;
}

/**
 * @brief Get the PercentSetting attribute.
 * The caller MUST hold the CHIP stack lock before calling this function,
 * unless calling from a CHIP task context where the lock is already held.
 */
DataModel::Nullable<Percent> ExtractorHoodEndpoint::GetPercentSetting() const
{
    DataModel::Nullable<Percent> percentSetting;

    Status status = Clusters::FanControl::Attributes::PercentSetting::Get(mEndpointId, percentSetting);

    VerifyOrReturnValue(status == Status::Success, DataModel::Nullable<Percent>(),
                        ChipLogError(NotSpecified,
                                     "ExtractorHoodEndpoint::GetPercentSetting: failed to get PercentSetting attribute: %d",
                                     to_underlying(status)));
    return percentSetting;
}

/**
 * @brief Get the FanMode attribute.
 * The caller MUST hold the CHIP stack lock before calling this function,
 * unless calling from a CHIP task context where the lock is already held.
 */
Status ExtractorHoodEndpoint::GetFanMode(FanControl::FanModeEnum & fanMode) const
{
    Status status = FanControl::Attributes::FanMode::Get(mEndpointId, &fanMode);

    VerifyOrReturnValue(status == Status::Success, status,
                        ChipLogError(NotSpecified, "ExtractorHoodEndpoint::GetFanMode: failed to get FanMode attribute: %d",
                                     to_underlying(status)));

    return Status::Success;
}

/**
 * @brief Set the PercentCurrent attribute if it differs from the current value.
 * The caller MUST hold the CHIP stack lock before calling this function,
 * unless calling from a CHIP task context where the lock is already held.
 */
Status ExtractorHoodEndpoint::SetPercentCurrent(Percent newPercentSetting)
{
    Percent currentPercentCurrent = 0;

    Status getStatus = FanControl::Attributes::PercentCurrent::Get(mEndpointId, &currentPercentCurrent);

    // Return error if we can't read current value
    VerifyOrReturnValue(getStatus == Status::Success, getStatus,
                        ChipLogError(NotSpecified,
                                     "ExtractorHoodEndpoint::HandlePercentSettingChange: failed to get currentPercentCurrent: %d",
                                     to_underlying(getStatus)));

    // No update needed if value is unchanged
    VerifyOrReturnValue(newPercentSetting != currentPercentCurrent, Status::Success);

    Status setStatus = FanControl::Attributes::PercentCurrent::Set(mEndpointId, newPercentSetting);

    VerifyOrReturnValue(setStatus == Status::Success, Status::Failure,
                        ChipLogError(NotSpecified,
                                     "ExtractorHoodEndpoint::SetPercentCurrent: failed to update PercentCurrent attribute: %d",
                                     to_underlying(setStatus)));
    return Status::Success;
}

/**
 * @brief Handle a change to the PercentSetting attribute, updating PercentCurrent as needed.
 * The caller MUST hold the CHIP stack lock before calling this function,
 * unless calling from a CHIP task context where the lock is already held.
 */
Status ExtractorHoodEndpoint::HandlePercentSettingChange(Percent newPercentSetting)
{
    ChipLogDetail(NotSpecified, "ExtractorHoodEndpoint::HandlePercentSettingChange: %d", newPercentSetting);
    // Get current PercentCurrent to check if it's different
    Percent currentPercentCurrent = 0;

    Status getStatus = FanControl::Attributes::PercentCurrent::Get(mEndpointId, &currentPercentCurrent);

    // Return error if we can't read current value
    VerifyOrReturnValue(getStatus == Status::Success, getStatus,
                        ChipLogError(NotSpecified,
                                     "ExtractorHoodEndpoint::HandlePercentSettingChange: failed to get PercentCurrent: %d",
                                     to_underlying(getStatus)));
    // No update needed if value is unchanged
    VerifyOrReturnValue(newPercentSetting != currentPercentCurrent, Status::Success);

    // Get current fan mode to check if it's Auto
    FanControl::FanModeEnum currentFanMode;

    Status fanModeStatus = FanControl::Attributes::FanMode::Get(mEndpointId, &currentFanMode);

    // Fail if we can't read fan mode
    VerifyOrReturnValue(fanModeStatus == Status::Success, Status::Failure,
                        ChipLogError(NotSpecified, "ExtractorHoodEndpoint::HandlePercentSettingChange: failed to get FanMode: %d",
                                     to_underlying(fanModeStatus)));

    // Don't update PercentCurrent if fan mode is Auto
    VerifyOrReturnValue(currentFanMode != FanControl::FanModeEnum::kAuto, Status::Success);

    // Update PercentCurrent to match PercentSetting
    Status setStatus = FanControl::Attributes::PercentCurrent::Set(mEndpointId, newPercentSetting);

    VerifyOrReturnValue(
        setStatus == Status::Success, Status::Failure,
        ChipLogError(NotSpecified,
                     "ExtractorHoodEndpoint::HandlePercentSettingChange: failed to update PercentCurrent attribute: %d",
                     to_underlying(setStatus)));
    return Status::Success;
}

Status ExtractorHoodEndpoint::HandleFanModeChange(chip::app::Clusters::FanControl::FanModeEnum newFanMode)
{
    ChipLogDetail(NotSpecified, "ExtractorHoodEndpoint::HandleFanModeChange: %d", (uint8_t) newFanMode);

    switch (newFanMode)
    {
    case FanControl::FanModeEnum::kOff: {
        return SetPercentCurrent(mFanModeOffPercent);
    }
    case FanControl::FanModeEnum::kLow: {
        return SetPercentCurrent(mFanModeLowPercent);
    }
    case FanControl::FanModeEnum::kMedium: {
        return SetPercentCurrent(mFanModeMediumPercent);
    }
    case FanControl::FanModeEnum::kOn:
    case FanControl::FanModeEnum::kHigh: {
        return SetPercentCurrent(mFanModeHighPercent);
    }
    case FanControl::FanModeEnum::kSmart:
    case FanControl::FanModeEnum::kAuto: {
        // For Auto/Smart modes, update the FanMode attribute to reflect the current mode
        return UpdateFanModeAttribute(newFanMode);
    }
    case FanControl::FanModeEnum::kUnknownEnumValue: {
        ChipLogProgress(NotSpecified, "ExtractorHoodEndpoint::HandleFanModeChange: Unknown");
        return Status::Success; // Don't treat unknown as error
    }
    default:
        return Status::Success;
    }
}

/**
 * @brief Update the FanMode attribute.
 * The caller MUST hold the CHIP stack lock before calling this function,
 * unless calling from a CHIP task context where the lock is already held.
 */
Status ExtractorHoodEndpoint::UpdateFanModeAttribute(FanControl::FanModeEnum newFanMode)
{
    Status setStatus = FanControl::Attributes::FanMode::Set(mEndpointId, newFanMode);

    VerifyOrReturnValue(setStatus == Status::Success, Status::Failure,
                        ChipLogError(NotSpecified,
                                     "ExtractorHoodEndpoint::UpdateFanModeAttribute: failed to update FanMode attribute: %d",
                                     to_underlying(setStatus)));
    return Status::Success;
}

/**
 * @brief Toggle fan mode between Off and High.
 * This is used for button press toggles.
 */
Status ExtractorHoodEndpoint::ToggleFanMode()
{
    FanControl::FanModeEnum currentFanMode = FanControl::FanModeEnum::kUnknownEnumValue;
    Status getStatus                       = GetFanMode(currentFanMode);

    if (getStatus != Status::Success || currentFanMode == FanControl::FanModeEnum::kUnknownEnumValue)
    {
        ChipLogError(NotSpecified, "ExtractorHoodEndpoint::ToggleFanMode: failed to get current fan mode");
        return Status::Failure;
    }

    FanControl::FanModeEnum target =
        (currentFanMode == FanControl::FanModeEnum::kOff) ? FanControl::FanModeEnum::kHigh : FanControl::FanModeEnum::kOff;

    return UpdateFanModeAttribute(target);
}
