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
using namespace chip::app::Clusters::ExtractorHood;

CHIP_ERROR ExtractorHoodEndpoint::Init()
{
    return CHIP_NO_ERROR;
}

Status FanDelegate::HandleStep(StepDirectionEnum aDirection, bool aWrap, bool aLowestOff)
{
    ChipLogProgress(Zcl, "FanDelegate::HandleStep direction=%d wrap=%d lowestOff=%d", to_underlying(aDirection), aWrap, aLowestOff);

    VerifyOrReturnError(aDirection != StepDirectionEnum::kUnknownEnumValue, Status::InvalidCommand);

    // if aLowestOff is true, Step command can reduce the fan to 0%, else 1%.
    uint8_t percentMin = aLowestOff ? ExtractorHoodEndpoint::kaLowestOffTrue : ExtractorHoodEndpoint::kaLowestOffFalse;
    uint8_t percentMax = 100;  // Maximum percent value

    // Get current percent setting from the attribute
    DataModel::Nullable<Percent> percentSettingNullable;
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    Status status = chip::app::Clusters::FanControl::Attributes::PercentSetting::Get(mEndpoint, percentSettingNullable);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    
    if (status != Status::Success)
    {
        ChipLogError(NotSpecified, "FanDelegate::HandleStep: failed to get PercentSetting attribute: %d", to_underlying(status));
    }

    uint8_t curPercentSetting = percentSettingNullable.IsNull() ? percentMin : percentSettingNullable.Value();

    // increase or decrease the fan percent by step size
    switch (aDirection)
    {
    case StepDirectionEnum::kIncrease: {
        if (curPercentSetting >= percentMax)
        {
            curPercentSetting = aWrap ? percentMin : percentMax;
        }
        else
        {
            curPercentSetting = std::min(percentMax, static_cast<uint8_t>(curPercentSetting + ExtractorHoodEndpoint::kStepSizePercent));
        }
        break;
    }
    case StepDirectionEnum::kDecrease: {
        if (curPercentSetting <= percentMin)
        {
            curPercentSetting = aWrap ? percentMax : percentMin;
        }
        else
        {
            curPercentSetting = std::max(percentMin, static_cast<uint8_t>(curPercentSetting - ExtractorHoodEndpoint::kStepSizePercent));
        }
        break;
    }
    default: {
        break;
    }
    }

    // HandleStep is called from CHIP stack thread, but locks are still needed
    // to protect attribute access from concurrent access by other threads (e.g., app task)
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    Status setStatus = chip::app::Clusters::FanControl::Attributes::PercentSetting::Set(mEndpoint, curPercentSetting);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    
    if (setStatus != Status::Success)
    {
        ChipLogError(NotSpecified, "FanDelegate::HandleStep: Failed to update PercentSetting attribute: %d", to_underlying(setStatus));
        return Status::Failure;
    }

    return Status::Success;
}

DataModel::Nullable<Percent> ExtractorHoodEndpoint::GetPercentSetting() const
{
    DataModel::Nullable<Percent> percentSetting;

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

Status ExtractorHoodEndpoint::GetFanMode(FanModeEnum & fanMode) const
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

Status ExtractorHoodEndpoint::SetPercentCurrent(Percent aNewPercentSetting)
{
    // Get current PercentCurrent to check if it's different
    Percent currentPercentCurrent = 0;
    
    chip::DeviceLayer::PlatformMgr().LockChipStack();
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

Status ExtractorHoodEndpoint::HandlePercentSettingChange(Percent aNewPercentSetting)
{
    ChipLogDetail(NotSpecified, "ExtractorHoodEndpoint::HandlePercentSettingChange: %d", aNewPercentSetting);
    
    // Get current PercentCurrent to check if it's different
    Percent currentPercentCurrent = 0;
    
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    Status getStatus = chip::app::Clusters::FanControl::Attributes::PercentCurrent::Get(mEndpointId, &currentPercentCurrent);
    
    // Don't update if the value is the same
    if (getStatus == Status::Success && aNewPercentSetting == currentPercentCurrent)
    {
        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        return Status::Success;
    }
    
    // Get current fan mode to check if it's Auto
    FanModeEnum currentFanMode;
    Status fanModeStatus = chip::app::Clusters::FanControl::Attributes::FanMode::Get(mEndpointId, &currentFanMode);
    
    // Don't update PercentCurrent if fan mode is Auto
    if (fanModeStatus == Status::Success && currentFanMode == FanModeEnum::kAuto)
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

Status ExtractorHoodEndpoint::HandleFanModeChange(FanModeEnum aNewFanMode)
{
    ChipLogDetail(NotSpecified, "ExtractorHoodEndpoint::HandleFanModeChange: %d", (uint8_t) aNewFanMode);
    
    switch (aNewFanMode)
    {
    case FanModeEnum::kOff: {
        return SetPercentCurrent(kFanModeOffPercent);
    }
    case FanModeEnum::kLow: {
        return SetPercentCurrent(kFanModeLowPercent);
    }
    case FanModeEnum::kMedium: {
        return SetPercentCurrent(kFanModeMediumPercent);
    }
    case FanModeEnum::kOn:
    case FanModeEnum::kHigh: {
        return SetPercentCurrent(kFanModeHighPercent);
    }
    case FanModeEnum::kSmart:
    case FanModeEnum::kAuto: {
        // For Auto/Smart modes, update the FanMode attribute to reflect the current mode
        return UpdateFanModeAttribute(aNewFanMode);
    }
    case FanModeEnum::kUnknownEnumValue: {
        ChipLogProgress(NotSpecified, "ExtractorHoodEndpoint::HandleFanModeChange: Unknown");
        return Status::Success; // Don't treat unknown as error
    }
    default:
        return Status::Success;
    }
}

Status ExtractorHoodEndpoint::UpdateFanModeAttribute(FanModeEnum aFanMode)
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
