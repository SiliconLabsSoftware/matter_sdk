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

#pragma once

#include <app/clusters/on-off-server/on-off-server.h>
#include <lib/core/CHIPError.h>
#include <lib/core/DataModelTypes.h>

class LightEndpoint
{
public:
    LightEndpoint(chip::EndpointId endpointId) : mEndpointId(endpointId) {}

    /**
     * @brief Initialize the Light endpoint.
     */
    CHIP_ERROR Init();

    /**
     * @brief Get the current On/Off state from the Matter attribute
     * @return true if light is on, false if off
     */
    bool GetOnOffState();

    /**
     * @brief Check if the light is currently on
     * @return true if light is on, false if off
     * @note This is a convenience method that calls GetOnOffState()
     */
    bool IsLightOn() { return GetOnOffState(); }

    /**
     * @brief Set On/Off state for the Light.
     */
    void SetOnOffState(bool state);

    /**
     * @brief Enable or disable auto turn-off feature
     * @param aOn true to enable auto turn-off, false to disable
     */
    void EnableAutoTurnOff(bool aOn);

    /**
     * @brief Set the duration for auto turn-off in seconds
     * @param aDurationInSecs Duration in seconds before auto turn-off
     */
    void SetAutoTurnOffDuration(uint32_t aDurationInSecs);

    /**
     * @brief Get auto turn-off enabled state
     * @return true if auto turn-off is enabled, false otherwise
     */
    bool IsAutoTurnOffEnabled() const { return mAutoTurnOff; }

    /**
     * @brief Get auto turn-off duration
     * @return Duration in seconds
     */
    uint32_t GetAutoTurnOffDuration() const { return mAutoTurnOffDuration; }

private:
    chip::EndpointId mEndpointId = chip::kInvalidEndpointId;
    bool mAutoTurnOff = false;
    uint32_t mAutoTurnOffDuration = 0;
};