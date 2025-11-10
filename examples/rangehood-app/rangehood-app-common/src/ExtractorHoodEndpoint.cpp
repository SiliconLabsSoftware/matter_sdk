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

#include <app/clusters/fan-control-server/fan-control-delegate.h>
#include <app/clusters/fan-control-server/fan-control-server.h>
#include <app-common/zap-generated/attributes/Accessors.h>
#include <lib/core/CHIPError.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>

#include <algorithm>

using namespace chip;
using namespace chip::DeviceLayer;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::FanControl;
using Status = chip::Protocols::InteractionModel::Status;

CHIP_ERROR ExtractorHoodEndpoint::Init(chip::Percent offPercent, chip::Percent lowPercent, 
                                      chip::Percent mediumPercent, chip::Percent highPercent)
{
    // Set fan mode percent mappings (must be set during initialization)
    mFanModeOffPercent    = offPercent;
    mFanModeLowPercent    = lowPercent;
    mFanModeMediumPercent = mediumPercent;
    mFanModeHighPercent   = highPercent;
    
    // Initialize percent current from percent setting
    // This ensures the fan speed reflects the current setting on startup
    chip::app::DataModel::Nullable<chip::Percent> percentSettingNullable = GetPercentSetting();
    chip::Percent percentSetting = percentSettingNullable.IsNull() ? 0 : percentSettingNullable.Value();
    HandlePercentSettingChange(percentSetting);
    return CHIP_NO_ERROR;
}

chip::app::DataModel::Nullable<chip::Percent> ExtractorHoodEndpoint::GetPercentSetting() const
{
    chip::app::DataModel::Nullable<chip::Percent> percentSetting;

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    Status status = chip::app::Clusters::FanControl::Attributes::PercentSetting::Get(mEndpointId, percentSetting);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    
    if (status != Status::Success)
    {
        ChipLogError(NotSpecified, "ExtractorHoodEndpoint::GetPercentSetting: failed to get PercentSetting attribute: %d",
                     to_underlying(status));
    }

    return percentSetting;
}

Status ExtractorHoodEndpoint::GetFanMode(chip::app::Clusters::FanControl::FanModeEnum & fanMode) const
{
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    Status status = chip::app::Clusters::FanControl::Attributes::FanMode::Get(mEndpointId, &fanMode);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    
    if (status != Status::Success)
    {
        ChipLogError(NotSpecified, "ExtractorHoodEndpoint::GetFanMode: failed to get FanMode attribute: %d",
                     to_underlying(status));
    }
    return status;
}

Status ExtractorHoodEndpoint::SetPercentCurrent(chip::Percent aNewPercentSetting)
{
    // Get current PercentCurrent to check if it's different, then update if needed
    // Keep lock held for entire read-modify-write operation to avoid race conditions
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    
    chip::Percent currentPercentCurrent = 0;
    Status getStatus = chip::app::Clusters::FanControl::Attributes::PercentCurrent::Get(mEndpointId, &currentPercentCurrent);
    
    // Only update if the value is different (or if we couldn't read the current value)
    if (getStatus != Status::Success || aNewPercentSetting != currentPercentCurrent)
    {
        Status setStatus = chip::app::Clusters::FanControl::Attributes::PercentCurrent::Set(mEndpointId, aNewPercentSetting);
        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        
        if (setStatus != Status::Success)
        {
            ChipLogError(NotSpecified, "ExtractorHoodEndpoint::SetPercentCurrent: failed to update PercentCurrent attribute: %d",
                         to_underlying(setStatus));
            return Status::Failure;
        }
    }
    else
    {
        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    }
    
    return Status::Success;
}

Status ExtractorHoodEndpoint::HandlePercentSettingChange(chip::Percent aNewPercentSetting)
{
    ChipLogDetail(NotSpecified, "ExtractorHoodEndpoint::HandlePercentSettingChange: %d", aNewPercentSetting);
    
    // Get current PercentCurrent to check if it's different
    chip::Percent currentPercentCurrent = 0;
    
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    Status getStatus = chip::app::Clusters::FanControl::Attributes::PercentCurrent::Get(mEndpointId, &currentPercentCurrent);
    
    // Return error if we can't read current value
    if (getStatus != Status::Success)
    {
        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        ChipLogError(NotSpecified, "ExtractorHoodEndpoint::HandlePercentSettingChange: failed to get PercentCurrent: %d", to_underlying(getStatus));
        return getStatus;
    }
    
    // Don't update if the value is the same
    if (aNewPercentSetting == currentPercentCurrent)
    {
        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        return Status::Success;
    }
    
    // Get current fan mode to check if it's Auto
    chip::app::Clusters::FanControl::FanModeEnum currentFanMode;
    Status fanModeStatus = chip::app::Clusters::FanControl::Attributes::FanMode::Get(mEndpointId, &currentFanMode);
    
    // If we can't read fan mode, log error but continue (fan mode check is optional optimization)
    if (fanModeStatus != Status::Success)
    {
        ChipLogError(NotSpecified, "ExtractorHoodEndpoint::HandlePercentSettingChange: failed to get FanMode: %d", to_underlying(fanModeStatus));
        // Continue - fan mode check is for optimization, not critical
    }
    
    // Don't update PercentCurrent if fan mode is Auto
    if (fanModeStatus == Status::Success && currentFanMode == chip::app::Clusters::FanControl::FanModeEnum::kAuto)
    {
        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        return Status::Success;
    }
    
    // Update PercentCurrent to match PercentSetting
    Status setStatus = chip::app::Clusters::FanControl::Attributes::PercentCurrent::Set(mEndpointId, aNewPercentSetting);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    
    if (setStatus != Status::Success)
    {
        ChipLogError(NotSpecified, "ExtractorHoodEndpoint::HandlePercentSettingChange: failed to update PercentCurrent attribute: %d",
                     to_underlying(setStatus));
        return Status::Failure;
    }
    
    return Status::Success;
}

Status ExtractorHoodEndpoint::HandleFanModeChange(chip::app::Clusters::FanControl::FanModeEnum aNewFanMode)
{
    ChipLogDetail(NotSpecified, "ExtractorHoodEndpoint::HandleFanModeChange: %d", (uint8_t) aNewFanMode);
    
    switch (aNewFanMode)
    {
    case chip::app::Clusters::FanControl::FanModeEnum::kOff: {
        return SetPercentCurrent(mFanModeOffPercent);
    }
    case chip::app::Clusters::FanControl::FanModeEnum::kLow: {
        return SetPercentCurrent(mFanModeLowPercent);
    }
    case chip::app::Clusters::FanControl::FanModeEnum::kMedium: {
        return SetPercentCurrent(mFanModeMediumPercent);
    }
    case chip::app::Clusters::FanControl::FanModeEnum::kOn:
    case chip::app::Clusters::FanControl::FanModeEnum::kHigh: {
        return SetPercentCurrent(mFanModeHighPercent);
    }
    case chip::app::Clusters::FanControl::FanModeEnum::kSmart:
    case chip::app::Clusters::FanControl::FanModeEnum::kAuto: {
        // For Auto/Smart modes, update the FanMode attribute to reflect the current mode
        return UpdateFanModeAttribute(aNewFanMode);
    }
    case chip::app::Clusters::FanControl::FanModeEnum::kUnknownEnumValue: {
        ChipLogProgress(NotSpecified, "ExtractorHoodEndpoint::HandleFanModeChange: Unknown");
        return Status::Success; // Don't treat unknown as error
    }
    default:
        return Status::Success;
    }
}

Status ExtractorHoodEndpoint::UpdateFanModeAttribute(chip::app::Clusters::FanControl::FanModeEnum aFanMode)
{
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    Status setStatus = chip::app::Clusters::FanControl::Attributes::FanMode::Set(mEndpointId, aFanMode);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    
    if (setStatus != Status::Success)
    {
        ChipLogError(NotSpecified, "ExtractorHoodEndpoint::UpdateFanModeAttribute: failed to update FanMode attribute: %d",
                     to_underlying(setStatus));
        return Status::Failure;
    }
    return Status::Success;
}

Status ExtractorHoodEndpoint::ToggleFanMode()
{
    chip::app::Clusters::FanControl::FanModeEnum currentFanMode = chip::app::Clusters::FanControl::FanModeEnum::kUnknownEnumValue;
    Status getStatus = GetFanMode(currentFanMode);
    
    if (getStatus != Status::Success || currentFanMode == chip::app::Clusters::FanControl::FanModeEnum::kUnknownEnumValue)
    {
        ChipLogError(NotSpecified, "ExtractorHoodEndpoint::ToggleFanMode: failed to get current fan mode");
        return Status::Failure;
    }
    
    chip::app::Clusters::FanControl::FanModeEnum target = 
        (currentFanMode == chip::app::Clusters::FanControl::FanModeEnum::kOff) 
            ? chip::app::Clusters::FanControl::FanModeEnum::kHigh 
            : chip::app::Clusters::FanControl::FanModeEnum::kOff;
    
    return UpdateFanModeAttribute(target);
}
