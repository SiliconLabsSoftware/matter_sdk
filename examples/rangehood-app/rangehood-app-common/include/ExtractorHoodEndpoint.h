/*
 *
 *    Copyright (c) 2024 Project CHIP Authors
 *    Copyright (c) 2019 Google LLC.
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

#include <app/clusters/fan-control-server/fan-control-delegate.h>
#include <app/clusters/fan-control-server/fan-control-server.h>
#include <app/data-model/Nullable.h>
#include <lib/core/CHIPError.h>
#include <lib/core/DataModelTypes.h>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters::FanControl;
using Protocols::InteractionModel::Status;

namespace chip {
namespace app {
namespace Clusters {
namespace ExtractorHood {

class FanDelegate : public FanControl::Delegate
{
public:
    /**
     * @brief
     *   This method handles the step command. This will happen as fast as possible.
     *   The step command logic is implemented directly here for better encapsulation.
     *   Since this is called from the CHIP stack thread, we can update attributes directly.
     *
     *   @param[in]  aDirection     the direction in which the speed should step
     *   @param[in]  aWrap          whether the speed should wrap or not
     *   @param[in]  aLowestOff     whether the device should consider the lowest setting as off
     *
     *   @return Success On success.
     *   @return Other Value indicating it failed to execute the command.
     */
    Status HandleStep(StepDirectionEnum aDirection, bool aWrap, bool aLowestOff) override;

    FanDelegate(EndpointId aEndpoint) 
        : FanControl::Delegate(aEndpoint), mEndpoint(aEndpoint) {}

protected:
    EndpointId mEndpoint = kInvalidEndpointId;
};

class ExtractorHoodEndpoint
{
public:
    ExtractorHoodEndpoint(EndpointId endpointId) : mEndpointId(endpointId), mFanDelegate(mEndpointId) {}

    /**
     * @brief Initialize the ExtractorHood endpoint.
     */
    CHIP_ERROR Init();

    // Accessor for registering the fan control delegate
    FanDelegate * GetFanDelegate() { return &mFanDelegate; }


    /**
     * @brief Get the endpoint ID
     */
    EndpointId GetEndpointId() const { return mEndpointId; }

    /**
     * @brief Get current percent setting from the attribute
     */
    DataModel::Nullable<Percent> GetPercentSetting() const;

    /**
     * @brief Get current fan mode from the attribute
     */
    Status GetFanMode(FanModeEnum & fanMode) const;

    /**
     * @brief Set percent current (actual fan speed)
     * This updates the PercentCurrent attribute which represents the actual fan speed
     */
    Status SetPercentCurrent(Percent aNewPercentSetting);

    /**
     * @brief Handle percent setting change and update percent current accordingly
     * This is called when the PercentSetting attribute changes and updates PercentCurrent
     * if the fan mode is not Auto and the value is different
     * 
     * @param aNewPercentSetting The new percent setting value
     * @return Status Success on success, error code otherwise
     */
    Status HandlePercentSettingChange(Percent aNewPercentSetting);

    /**
     * @brief Handle fan mode change and update percent current accordingly
     * This maps fan modes to their corresponding percent values and updates the PercentCurrent attribute
     * 
     * @param aNewFanMode The new fan mode to apply
     * @return Status Success on success, error code otherwise
     */
    Status HandleFanModeChange(FanModeEnum aNewFanMode);

    /**
     * @brief Update the FanMode attribute
     * This is used for modes like Auto/Smart that don't have fixed percent values
     */
    Status UpdateFanModeAttribute(FanModeEnum aFanMode);

    // Step command configuration constants
    static constexpr uint8_t kStepSizePercent  = 10;  // Step by 10% increments
    static constexpr uint8_t kaLowestOffTrue    = 0;   // Can step down to 0% (off)
    static constexpr uint8_t kaLowestOffFalse   = 1;   // Can only step down to 1% (minimum on)

    // Fan Mode Percent Mappings (since SPEED features are not enabled)
    static constexpr uint8_t kFanModeOffPercent    = 0;    // Off: 0%
    static constexpr uint8_t kFanModeLowPercent    = 30;   // Low: 30%
    static constexpr uint8_t kFanModeMediumPercent = 60;   // Medium: 60%
    static constexpr uint8_t kFanModeHighPercent   = 100;  // High: 100%

private:
    EndpointId mEndpointId = kInvalidEndpointId;
    FanDelegate mFanDelegate;

} // namespace ExtractorHood
} // namespace Clusters
} // namespace app
} // namespace chip
