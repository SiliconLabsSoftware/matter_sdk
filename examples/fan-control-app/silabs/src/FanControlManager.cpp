/*
 *
 *    Copyright (c) 2024 Project CHIP Authors
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

#include "FanControlManager.h"
#if defined(DISPLAY_ENABLED) && DISPLAY_ENABLED
#include "FanControlUI.h"
#endif

#include "AppConfig.h"
#include "AppTask.h"

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/AttributeAccessInterfaceRegistry.h>
#include <lib/support/TypeTraits.h>

using namespace chip;
using namespace chip::app;
using namespace ::chip::app::Clusters::FanControl;
using Protocols::InteractionModel::Status;

FanControlManager FanControlManager::sFan(FanControlMgr().GetEndPoint());

#ifdef SL_CATALOG_SIMPLE_LED_LED1_PRESENT
#define FAN_LED 1
#else
#define FAN_LED 0
#endif

namespace {
LEDWidget sFanLED;

uint8_t SpeedToPercent(uint8_t speed, uint8_t speedMax)
{
    if (speedMax == 0)
    {
        return 0;
    }
    return static_cast<uint8_t>((static_cast<uint16_t>(speed) * 100) / speedMax);
}
} // namespace

bool FanControlManager::SupportsMultiSpeed() const
{
    uint32_t featureMap = 0;
    if (Attributes::FeatureMap::Get(FanControlMgr().GetEndPoint(), &featureMap) != Status::Success)
    {
        return false;
    }
    return (featureMap & to_underlying(Feature::kMultiSpeed)) != 0;
}

FanModeEnum FanControlManager::DeriveFanModeFromSpeed(uint8_t speed) const
{
    if (speed == 0)
    {
        return FanModeEnum::kOff;
    }
    if (speed <= kFanModeLowUpperBound)
    {
        return FanModeEnum::kLow;
    }
    if (speed <= kFanModeMediumUpperBound)
    {
        return FanModeEnum::kMedium;
    }
    return FanModeEnum::kHigh;
}

FanModeEnum FanControlManager::DeriveFanModeFromPercent(Percent percent) const
{
    if (percent == 0)
    {
        return FanModeEnum::kOff;
    }
    if (percent <= SpeedToPercent(static_cast<uint8_t>(kFanModeLowUpperBound), mSpeedMax))
    {
        return FanModeEnum::kLow;
    }
    if (percent <= SpeedToPercent(static_cast<uint8_t>(kFanModeMediumUpperBound), mSpeedMax))
    {
        return FanModeEnum::kMedium;
    }
    return FanModeEnum::kHigh;
}

CHIP_ERROR FanControlManager::Init()
{
    SetDefaultDelegate(GetEndPoint(), this);
    sFanLED.Init(FAN_LED);
    AppTask::GetAppTask().LinkAppLed(&sFanLED);

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    mSupportsMultiSpeed = SupportsMultiSpeed();
    if (Attributes::SpeedMax::Get(GetEndPoint(), &mSpeedMax) != Status::Success || mSpeedMax == 0)
    {
        mSpeedMax = static_cast<uint8_t>(kFanModeHighLowerBound);
    }
    DataModel::Nullable<Percent> percentSettingNullable = GetPercentSetting();
    DataModel::Nullable<uint8_t> speedSettingNullable   = GetSpeedSetting();
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    uint8_t percentSettingCB = percentSettingNullable.IsNull() ? 0 : percentSettingNullable.Value();
    PercentSettingWriteCallback(percentSettingCB);

    if (mSupportsMultiSpeed)
    {
        uint8_t speedSettingCB = speedSettingNullable.IsNull() ? 0 : speedSettingNullable.Value();
        SpeedSettingWriteCallback(speedSettingCB);
    }

    return CHIP_NO_ERROR;
}

EndpointId FanControlManager::GetEndPoint()
{
    return mEndPoint;
}

Status FanControlManager::HandleStep(StepDirectionEnum aDirection, bool aWrap, bool aLowestOff)
{
    ChipLogProgress(NotSpecified, "FanControlManager::HandleStep aDirection %d, aWrap %d, aLowestOff %d", to_underlying(aDirection),
                    aWrap, aLowestOff);

    VerifyOrReturnError(aDirection != StepDirectionEnum::kUnknownEnumValue, Status::InvalidCommand);

    uint8_t speedMin = aLowestOff ? kaLowestOffTrue : kaLowestOffFalse;

    if (mSupportsMultiSpeed)
    {
        DataModel::Nullable<uint8_t> speedSettingNullable = GetSpeedSetting();
        uint8_t curSpeedSetting                         = speedSettingNullable.IsNull() ? speedMin : speedSettingNullable.Value();

        switch (aDirection)
        {
        case StepDirectionEnum::kIncrease: {
            if (curSpeedSetting >= mSpeedMax)
            {
                curSpeedSetting = aWrap ? speedMin : mSpeedMax;
            }
            else
            {
                curSpeedSetting++;
            }
            break;
        }
        case StepDirectionEnum::kDecrease: {
            if (curSpeedSetting <= speedMin)
            {
                curSpeedSetting = aWrap ? mSpeedMax : speedMin;
            }
            else
            {
                curSpeedSetting--;
            }
            break;
        }
        default: {
            break;
        }
        }

        SetSpeedSetting(curSpeedSetting);
    }
    else
    {
        DataModel::Nullable<Percent> percentSettingNullable = GetPercentSetting();
        Percent curPercentSetting = percentSettingNullable.IsNull() ? static_cast<Percent>(speedMin) : percentSettingNullable.Value();

        switch (aDirection)
        {
        case StepDirectionEnum::kIncrease: {
            if (curPercentSetting >= 100)
            {
                curPercentSetting = aWrap ? static_cast<Percent>(speedMin) : 100;
            }
            else
            {
                curPercentSetting++;
            }
            break;
        }
        case StepDirectionEnum::kDecrease: {
            if (curPercentSetting <= speedMin)
            {
                curPercentSetting = aWrap ? 100 : static_cast<Percent>(speedMin);
            }
            else
            {
                curPercentSetting--;
            }
            break;
        }
        default: {
            break;
        }
        }

        SetPercentSetting(curPercentSetting);
    }

    return Status::Success;
}

void FanControlManager::UpdateClusterState(intptr_t arg)
{
    FanControlManager::AttributeUpdateInfo * data = reinterpret_cast<FanControlManager::AttributeUpdateInfo *>(arg);
    if (data->isSpeedCurrent)
    {
        Attributes::SpeedCurrent::Set(data->endPoint, data->speedCurrent);
    }
    else if (data->isPercentCurrent)
    {
        Attributes::PercentCurrent::Set(data->endPoint, data->percentCurrent);
    }
    else if (data->isSpeedSetting)
    {
        Attributes::SpeedSetting::Set(data->endPoint, data->speedSetting);
    }
    else if (data->isFanMode)
    {
        Attributes::FanMode::Set(data->endPoint, data->fanMode);
    }
    else if (data->isPercentSetting)
    {
        Attributes::PercentSetting::Set(data->endPoint, data->percentSetting);
    }
    chip::Platform::Delete(data);
}

void FanControlManager::HandleFanControlAttributeChange(AttributeId attributeId, uint8_t type, uint16_t size, uint8_t * value)
{
    switch (attributeId)
    {
    case Attributes::PercentSetting::Id: {
        PercentSettingWriteCallback(*value);
        break;
    }

    case Attributes::SpeedSetting::Id: {
        if (mSupportsMultiSpeed)
        {
            SpeedSettingWriteCallback(*value);
        }
        break;
    }

    case Attributes::FanMode::Id: {
        mFanMode = *reinterpret_cast<FanModeEnum *>(value);
        ApplyFanModeMapping(mFanMode);
        UpdateFanControlLED();
#if defined(DISPLAY_ENABLED) && DISPLAY_ENABLED
        UpdateFanControlLCD();
#endif
        break;
    }

    default: {
        break;
    }
    }
}

void FanControlManager::PercentSettingWriteCallback(uint8_t aNewPercentSetting)
{
    VerifyOrReturn(aNewPercentSetting != percentCurrent);
    VerifyOrReturn(mFanMode != FanModeEnum::kAuto);
    ChipLogDetail(NotSpecified, "FanControlManager::PercentSettingWriteCallback: %d", aNewPercentSetting);
    percentCurrent = aNewPercentSetting;

    AttributeUpdateInfo * data = chip::Platform::New<AttributeUpdateInfo>();
    data->endPoint             = GetEndPoint();
    data->percentCurrent       = percentCurrent;
    data->isPercentCurrent     = true;

    if (chip::DeviceLayer::PlatformMgr().ScheduleWork(UpdateClusterState, reinterpret_cast<intptr_t>(data)) != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "FanControlManager::PercentSettingWriteCallback: failed to set PercentCurrent attribute");
    }

    if (!mSupportsMultiSpeed)
    {
        FanModeEnum newFanMode = DeriveFanModeFromPercent(aNewPercentSetting);
        if (newFanMode != mFanMode)
        {
            SyncFanMode(newFanMode);
        }
    }
}

void FanControlManager::SpeedSettingWriteCallback(uint8_t aNewSpeedSetting)
{
    VerifyOrReturn(mSupportsMultiSpeed);
    VerifyOrReturn(aNewSpeedSetting != speedCurrent);
    VerifyOrReturn(mFanMode != FanModeEnum::kAuto);
    ChipLogDetail(NotSpecified, "FanControlManager::SpeedSettingWriteCallback: %d", aNewSpeedSetting);
    speedCurrent = aNewSpeedSetting;

    AttributeUpdateInfo * data = chip::Platform::New<AttributeUpdateInfo>();
    data->endPoint             = GetEndPoint();
    data->speedCurrent         = speedCurrent;
    data->isSpeedCurrent       = true;

    if (chip::DeviceLayer::PlatformMgr().ScheduleWork(UpdateClusterState, reinterpret_cast<intptr_t>(data)) != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "FanControlManager::SpeedSettingWriteCallback: failed to set SpeedCurrent attribute");
    }

    FanModeEnum newFanMode = DeriveFanModeFromSpeed(aNewSpeedSetting);
    if (newFanMode != mFanMode)
    {
        SyncFanMode(newFanMode);
    }
}

void FanControlManager::SyncFanMode(FanModeEnum aNewFanMode)
{
    FanModeEnum currentFanMode;
    if (Attributes::FanMode::Get(GetEndPoint(), &currentFanMode) == Status::Success && currentFanMode == aNewFanMode)
    {
        mFanMode = aNewFanMode;
        return;
    }

    mFanMode                   = aNewFanMode;
    AttributeUpdateInfo * data = chip::Platform::New<AttributeUpdateInfo>();
    data->endPoint             = GetEndPoint();
    data->fanMode              = mFanMode;
    data->isFanMode            = true;
    if (chip::DeviceLayer::PlatformMgr().ScheduleWork(UpdateClusterState, reinterpret_cast<intptr_t>(data)) != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "FanControlManager::SyncFanMode: failed to update FanMode attribute");
    }
}

void FanControlManager::ApplyFanModeMapping(FanModeEnum aNewFanMode)
{
    ChipLogDetail(NotSpecified, "FanControlManager::ApplyFanModeMapping: %d", to_underlying(aNewFanMode));

    if (mSupportsMultiSpeed)
    {
        uint8_t speedSettingCurrent = GetSpeedSetting().ValueOr(101);
        uint8_t highSpeedSetting    = (mSpeedMax >= kFanModeHighLowerBound) ? static_cast<uint8_t>(kFanModeHighLowerBound) : mSpeedMax;

        switch (aNewFanMode)
        {
        case FanModeEnum::kOff: {
            if (speedSettingCurrent != 0)
            {
                SetSpeedSetting(0);
            }
            break;
        }
        case FanModeEnum::kLow: {
            if (speedSettingCurrent < kFanModeLowLowerBound || speedSettingCurrent > kFanModeLowUpperBound)
            {
                SetSpeedSetting(static_cast<uint8_t>(kFanModeLowLowerBound));
            }
            break;
        }
        case FanModeEnum::kMedium: {
            if (speedSettingCurrent < kFanModeMediumLowerBound || speedSettingCurrent > kFanModeMediumUpperBound)
            {
                SetSpeedSetting(static_cast<uint8_t>(kFanModeMediumLowerBound));
            }
            break;
        }
        case FanModeEnum::kOn:
        case FanModeEnum::kHigh: {
            if (speedSettingCurrent < highSpeedSetting || speedSettingCurrent > mSpeedMax)
            {
                SetSpeedSetting(highSpeedSetting);
            }
            break;
        }
        case FanModeEnum::kSmart:
        case FanModeEnum::kAuto: {
            ChipLogProgress(NotSpecified, "FanControlManager::ApplyFanModeMapping: Auto");
            break;
        }
        case FanModeEnum::kUnknownEnumValue: {
            ChipLogProgress(NotSpecified, "FanControlManager::ApplyFanModeMapping: Unknown");
            break;
        }
        default:
            break;
        }
    }
    else
    {
        uint8_t percentSettingCurrent = GetPercentSetting().ValueOr(101);
        const uint8_t lowPercentMin   = 1;
        const uint8_t lowPercentMax   = SpeedToPercent(static_cast<uint8_t>(kFanModeLowUpperBound), mSpeedMax);
        const uint8_t mediumPercentMin =
            SpeedToPercent(static_cast<uint8_t>(kFanModeMediumLowerBound), mSpeedMax);
        const uint8_t mediumPercentMax =
            SpeedToPercent(static_cast<uint8_t>(kFanModeMediumUpperBound), mSpeedMax);
        const uint8_t highPercentMin =
            SpeedToPercent(static_cast<uint8_t>((mSpeedMax >= kFanModeHighLowerBound) ? kFanModeHighLowerBound : mSpeedMax),
                           mSpeedMax);
        const uint8_t highPercentMax = 100;

        switch (aNewFanMode)
        {
        case FanModeEnum::kOff: {
            if (percentSettingCurrent != 0)
            {
                SetPercentSetting(0);
            }
            break;
        }
        case FanModeEnum::kLow: {
            if (percentSettingCurrent < lowPercentMin || percentSettingCurrent > lowPercentMax)
            {
                SetPercentSetting(SpeedToPercent(static_cast<uint8_t>(kFanModeLowLowerBound), mSpeedMax));
            }
            break;
        }
        case FanModeEnum::kMedium: {
            if (percentSettingCurrent < mediumPercentMin || percentSettingCurrent > mediumPercentMax)
            {
                SetPercentSetting(mediumPercentMin);
            }
            break;
        }
        case FanModeEnum::kOn:
        case FanModeEnum::kHigh: {
            if (percentSettingCurrent < highPercentMin || percentSettingCurrent > highPercentMax)
            {
                SetPercentSetting(highPercentMin);
            }
            break;
        }
        case FanModeEnum::kSmart:
        case FanModeEnum::kAuto: {
            ChipLogProgress(NotSpecified, "FanControlManager::ApplyFanModeMapping: Auto");
            break;
        }
        case FanModeEnum::kUnknownEnumValue: {
            ChipLogProgress(NotSpecified, "FanControlManager::ApplyFanModeMapping: Unknown");
            break;
        }
        default:
            break;
        }
    }
}

void FanControlManager::SetSpeedSetting(uint8_t aNewSpeedSetting)
{
    VerifyOrReturn(mSupportsMultiSpeed);

    DataModel::Nullable<uint8_t> speedSettingNullable = GetSpeedSetting();
    if (!speedSettingNullable.IsNull() && speedSettingNullable.Value() == aNewSpeedSetting)
    {
        return;
    }

    Status status = Attributes::SpeedSetting::Set(GetEndPoint(), aNewSpeedSetting);
    if (status != Status::Success)
    {
        ChipLogError(NotSpecified, "FanControlManager::SetSpeedSetting: failed to set SpeedSetting attribute: %d",
                     to_underlying(status));
    }
}

void FanControlManager::SetPercentSetting(Percent aNewPercentSetting)
{
    DataModel::Nullable<Percent> percentSettingNullable = GetPercentSetting();
    if (!percentSettingNullable.IsNull() && percentSettingNullable.Value() == aNewPercentSetting)
    {
        return;
    }

    AttributeUpdateInfo * data = chip::Platform::New<AttributeUpdateInfo>();
    data->percentSetting       = aNewPercentSetting;
    data->endPoint             = GetEndPoint();
    data->isPercentSetting     = true;
    if (chip::DeviceLayer::PlatformMgr().ScheduleWork(UpdateClusterState, reinterpret_cast<intptr_t>(data)) != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "FanControlManager::SetPercentSetting: failed to update PercentSetting attribute");
    }
}

DataModel::Nullable<uint8_t> FanControlManager::GetSpeedSetting()
{
    DataModel::Nullable<uint8_t> speedSetting;

    Status status = Attributes::SpeedSetting::Get(GetEndPoint(), speedSetting);
    if (status != Status::Success)
    {
        ChipLogError(NotSpecified, "FanControlManager::GetSpeedSetting: failed to get SpeedSetting attribute: %d",
                     to_underlying(status));
    }

    return speedSetting;
}

DataModel::Nullable<Percent> FanControlManager::GetPercentSetting()
{
    DataModel::Nullable<Percent> percentSetting;

    Status status = Attributes::PercentSetting::Get(GetEndPoint(), percentSetting);
    if (status != Status::Success)
    {
        ChipLogError(NotSpecified, "FanControlManager::GetPercentSetting: failed to get PercentSetting attribute: %d",
                     to_underlying(status));
    }

    return percentSetting;
}

FanModeEnum FanControlManager::GetFanMode()
{
    return mFanMode;
}

void FanControlManager::UpdateFanControlLED()
{
    sFanLED.Set(false);
    FanModeEnum fanState = FanControlMgr().GetFanMode();
    if (fanState != FanModeEnum::kOff && fanState != FanModeEnum::kUnknownEnumValue)
    {
        sFanLED.Set(true);
    }
}

void FanControlManager::UpdateFanControlLCD()
{
    AppTask::GetAppTask().UpdateFanControlUI();
}
