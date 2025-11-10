/*
 *
 *    Copyright (c) 2025 Project CHIP Authors
 *    Copyright (c) 2025 Google LLC.
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

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <app/clusters/fan-control-server/fan-control-server.h>
#include <app/data-model/Nullable.h>
#include <lib/core/CHIPError.h>
#include <lib/core/DataModelTypes.h>

class ExtractorHoodEndpoint
{
public:
    ExtractorHoodEndpoint(chip::EndpointId endpointId) : mEndpointId(endpointId) {}

    /**
     * @brief Initialize the ExtractorHood endpoint.
     * @param offPercent Percent value for Off mode (typically 0)
     * @param lowPercent Percent value for Low mode (typically 30)
     * @param mediumPercent Percent value for Medium mode (typically 60)
     * @param highPercent Percent value for High/On mode (typically 100)
     */
    CHIP_ERROR Init(chip::Percent offPercent, chip::Percent lowPercent, chip::Percent mediumPercent, chip::Percent highPercent);

    chip::EndpointId GetEndpointId() const { return mEndpointId; }

    chip::app::DataModel::Nullable<chip::Percent> GetPercentSetting() const;

    chip::Protocols::InteractionModel::Status GetFanMode(chip::app::Clusters::FanControl::FanModeEnum & fanMode) const;

    chip::Protocols::InteractionModel::Status SetPercentCurrent(chip::Percent aNewPercentSetting);

    /**
     * @brief Handle percent setting change and update percent current accordingly
     * This is called when the PercentSetting attribute changes and updates PercentCurrent
     * if the fan mode is not Auto and the value is different
     *
     * @param aNewPercentSetting The new percent setting value
     * @return Status Success on success, error code otherwise
     */
    chip::Protocols::InteractionModel::Status HandlePercentSettingChange(chip::Percent aNewPercentSetting);

    /**
     * @brief Handle fan mode change and update percent current accordingly
     * This maps fan modes to their corresponding percent values and updates the PercentCurrent attribute
     *
     * @param aNewFanMode The new fan mode to apply
     * @return Status Success on success, error code otherwise
     */
    chip::Protocols::InteractionModel::Status HandleFanModeChange(chip::app::Clusters::FanControl::FanModeEnum aNewFanMode);

    /**
     * @brief Update the FanMode attribute
     */
    chip::Protocols::InteractionModel::Status UpdateFanModeAttribute(chip::app::Clusters::FanControl::FanModeEnum aFanMode);

    /**
     * @brief Toggle fan mode between Off and High
     * This is typically used for button press toggles
     * @return Status Success on success, error code otherwise
     */
    chip::Protocols::InteractionModel::Status ToggleFanMode();

private:
    chip::EndpointId mEndpointId = chip::kInvalidEndpointId;

    // Fan Mode Percent Mappings (set during initialization)
    chip::Percent mFanModeOffPercent    = 0;   // Off: 0%
    chip::Percent mFanModeLowPercent    = 30;  // Low: 30%
    chip::Percent mFanModeMediumPercent = 60;  // Medium: 60%
    chip::Percent mFanModeHighPercent   = 100; // High: 100%
};
