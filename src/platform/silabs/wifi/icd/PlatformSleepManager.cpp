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

#include <app/icd/server/ICDConfigurationData.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/silabs/wifi/WifiInterfaceAbstraction.h>
#include <platform/silabs/wifi/icd/PlatformSleepManager.h>

using namespace chip::app;

namespace chip {
namespace DeviceLayer {
namespace Silabs {

// Initialize the static instance
PlatformSleepManager PlatformSleepManager::mInstance;

CHIP_ERROR PlatformSleepManager::Init()
{
    return CHIP_NO_ERROR;
}

void PlatformSleepManager::HandleCommissioningComplete()
{
    TransitionToLowPowerMode();
}

void PlatformSleepManager::HandleInternetConnectivityChange()
{
    if (!isCommissioningInProgress)
    {
        TransitionToLowPowerMode();
    }
}

void PlatformSleepManager::HandleCommissioningWindowClose() {}

void PlatformSleepManager::HandleCommissioningSessionStarted()
{
    isCommissioningInProgress = true;
}

void PlatformSleepManager::HandleCommissioningSessionStopped()
{
    isCommissioningInProgress = false;
}

CHIP_ERROR PlatformSleepManager::RequestHighPerformance()
{
    VerifyOrReturnError(mHighPerformanceRequestCounter < std::numeric_limits<uint8_t>::max(), CHIP_ERROR_INTERNAL,
                        ChipLogError(DeviceLayer, "High performance request counter overflow"));

    if (mHighPerformanceRequestCounter == 0)
    {
#if SLI_SI917 // 917 SoC & NCP
        VerifyOrReturnError(wfx_power_save(RSI_ACTIVE, HIGH_PERFORMANCE, 0) == SL_STATUS_OK, CHIP_ERROR_INTERNAL,
                            ChipLogError(DeviceLayer, "Failed to set Wi-FI configuration to HighPerformance"));
        VerifyOrReturnError(ConfigureBroadcastFilter(false) == SL_STATUS_OK, CHIP_ERROR_INTERNAL,
                            ChipLogError(DeviceLayer, "Failed to disable broadcast filter"));
#endif // SLI_SI917
    }

    mHighPerformanceRequestCounter++;
    return CHIP_NO_ERROR;
}

CHIP_ERROR PlatformSleepManager::RemoveHighPerformanceRequest()
{
    VerifyOrReturnError(mHighPerformanceRequestCounter > 0, CHIP_NO_ERROR,
                        ChipLogError(DeviceLayer, "Wi-Fi configuration already in low power mode"));

    mHighPerformanceRequestCounter--;

    if (mHighPerformanceRequestCounter == 0)
    {
        ReturnErrorOnFailure(TransitionToLowPowerMode());
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR PlatformSleepManager::TransitionToLowPowerMode()
{
    VerifyOrReturnError(mHighPerformanceRequestCounter == 0, CHIP_NO_ERROR,
                        ChipLogDetail(DeviceLayer, "High Performance Requested - Device cannot go to a lower power mode."));

#if SLI_SI917 // 917 SoC & NCP
    // TODO: Clean this up when the Wi-Fi interface re-work is finished
    wfx_wifi_provision_t wifiConfig;
    wfx_get_wifi_provision(&wifiConfig);

    if (!(wifiConfig.ssid[0] != 0) && !isCommissioningInProgress)
    {
        VerifyOrReturnError(wfx_power_save(RSI_SLEEP_MODE_8, DEEP_SLEEP_WITH_RAM_RETENTION, 0) != SL_STATUS_OK, CHIP_ERROR_INTERNAL,
                            ChipLogError(DeviceLayer, "Failed to enable Deep Sleep."));
    }
    else
    {
        VerifyOrReturnError(wfx_power_save(RSI_SLEEP_MODE_2, ASSOCIATED_POWER_SAVE, 0) != SL_STATUS_OK, CHIP_ERROR_INTERNAL,
                            ChipLogError(DeviceLayer, "Failed to enable to go to sleep."));
    }
#elif RS911X_WIFI // rs9116
    VerifyOrReturnError(wfx_power_save() != SL_STATUS_OK, CHIP_ERROR_INTERNAL,
                        ChipLogError(DeviceLayer, "Failed to enable to go to sleep."));
#endif

    return CHIP_NO_ERROR;
}

} // namespace Silabs
} // namespace DeviceLayer
} // namespace chip
