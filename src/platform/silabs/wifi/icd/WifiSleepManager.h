/*
 *
 *    Copyright (c) 2024 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#pragma once

#include <lib/core/CHIPError.h>

namespace chip {
namespace DeviceLayer {
namespace Silabs {

/**
 * @brief WifiSleepManager is a singleton class that manages the sleep modes for Wi-Fi devices.
 *        The class contains the buisness logic associated with optimizing the sleep states based on the Matter SDK internal states
 */
class WifiSleepManager
{
public:
    WifiSleepManager(const WifiSleepManager &)             = delete;
    WifiSleepManager & operator=(const WifiSleepManager &) = delete;

    static WifiSleepManager & GetInstance() { return mInstance; }

    /**
     * @brief Init function that configure the SleepManager APIs based on the type of ICD.
     *        Function validates that the SleepManager configuration were correctly set as well.
     *
     * @return CHIP_ERROR
     */
    CHIP_ERROR Init();

    /**
     * @brief Public API to request the Wi-Fi chip to transition to High Performance.
     *        Function increases the HighPerformance request counter to prevent the chip from going to sleep
     *        while the Matter SDK is in a state that requires High Performance
     *
     * @return CHIP_ERROR CHIP_NO_ERROR if the chip was set to high performance or already in high performance
     *                    CHIP_ERROR_INTERNAL, if the high performance configuration failed
     */
    CHIP_ERROR RequestHighPerformance();

    /**
     * @brief Public API to remove request to keep the Wi-Fi chip in High Performance.
     *        If calling this function removes the last High performance request,
     *        The chip will transition to sleep based on its lowest sleep level allowed
     *
     * @return CHIP_ERROR CHIP_NO_ERROR if the req removal and sleep transition succeed
     *                    CHIP_ERROR_INTERNAL, if the req removal or the transition to sleep failed
     */
    CHIP_ERROR RemoveHighPerformanceRequest();

    void HandleCommissioningComplete();
    void HandleInternetConnectivityChange();
    void HandleCommissioningWindowClose();
    void HandleCommissioningSessionStarted();
    void HandleCommissioningSessionStopped();

private:
    WifiSleepManager()  = default;
    ~WifiSleepManager() = default;

    /**
     * @brief Transition the device to the Lowest Power State.
     *
     * @return CHIP_ERROR CHIP_NO_ERROR if the device was transitionned to low power
     *         CHIP_ERROR_INTERNAL if an error occured
     */
    CHIP_ERROR TransitionToLowPowerMode();

    static WifiSleepManager mInstance;
    bool isCommissioningInProgress = false;

    uint8_t mHighPerformanceRequestCounter = 0;
};

} // namespace Silabs
} // namespace DeviceLayer
} // namespace chip
