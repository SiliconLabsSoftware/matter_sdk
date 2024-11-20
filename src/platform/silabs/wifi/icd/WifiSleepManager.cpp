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
#include <platform/silabs/wifi/icd/WifiSleepManager.h>

namespace chip {
namespace DeviceLayer {
namespace Silabs {

// Initialize the static instance
WifiSleepManager WifiSleepManager::mInstance;

CHIP_ERROR WifiSleepManager::Init()
{
    return CHIP_NO_ERROR;
}

CHIP_ERROR WifiSleepManager::RequestHighPerformance()
{
    VerifyOrReturnError(mHighPerformanceRequestCounter < std::numeric_limits<uint8_t>::max(), CHIP_ERROR_INTERNAL,
                        ChipLogError(DeviceLayer, "High performance request counter overflow"));

    mHighPerformanceRequestCounter++;

    // We don't do the mHighPerformanceRequestCounter check here; the check is in TransitionToLowPowerMode function
    ReturnErrorOnFailure(VerifyAndTransitionToLowPowerMode());

    return CHIP_NO_ERROR;
}

CHIP_ERROR WifiSleepManager::RemoveHighPerformanceRequest()
{
    VerifyOrReturnError(mHighPerformanceRequestCounter > 0, CHIP_NO_ERROR,
                        ChipLogError(DeviceLayer, "Wi-Fi configuration already in low power mode"));

    mHighPerformanceRequestCounter--;

    // We don't do the mHighPerformanceRequestCounter check here; the check is in TransitionToLowPowerMode function
    ReturnErrorOnFailure(VerifyAndTransitionToLowPowerMode());

    return CHIP_NO_ERROR;
}

CHIP_ERROR WifiSleepManager::VerifyAndTransitionToLowPowerMode()
{

#if SLI_SI917 // 917 SoC & NCP
    if (mHighPerformanceRequestCounter > 0)
    {
        VerifyOrReturnError(ConfigureHighPerformance() == SL_STATUS_OK, CHIP_ERROR_INTERNAL);
        return CHIP_NO_ERROR;
    }

    if (mIsCommissioningInProgress)
    {
        VerifyOrReturnError(ConfigureDTIMBasedSleep() == SL_STATUS_OK, CHIP_ERROR_INTERNAL);
        return CHIP_NO_ERROR;
    }

    // TODO: Clean this up when the Wi-Fi interface re-work is finished
    wfx_wifi_provision_t wifiConfig;
    wfx_get_wifi_provision(&wifiConfig);

    if (!(wifiConfig.ssid[0] != 0))
    {
        VerifyOrReturnError(ConfigurePowerSave(RSI_SLEEP_MODE_8, DEEP_SLEEP_WITH_RAM_RETENTION, 0) == SL_STATUS_OK,
                            CHIP_ERROR_INTERNAL);
        return CHIP_NO_ERROR;
    }

    if (mCallback && mCallback->CanGoToLIBasedSleep())
    {
        VerifyOrReturnError(ConfigureLIBasedSleep() == SL_STATUS_OK, CHIP_ERROR_INTERNAL);
        return CHIP_NO_ERROR;
    }

    VerifyOrReturnError(ConfigureDTIMBasedSleep() == SL_STATUS_OK, CHIP_ERROR_INTERNAL);

#elif RS911X_WIFI // rs9116
    VerifyOrReturnError(ConfigurePowerSave() == SL_STATUS_OK, CHIP_ERROR_INTERNAL);
#endif

    return CHIP_NO_ERROR;
}

#if SLI_SI917 // 917 SoC & NCP
sl_status_t WifiSleepManager::ConfigureLIBasedSleep()
{
    sl_status_t status = SL_STATUS_OK;

    status = ConfigurePowerSave(RSI_SLEEP_MODE_2, ASSOCIATED_POWER_SAVE,
                                ICDConfigurationData::GetInstance().GetSlowPollingInterval().count());
    VerifyOrReturnError(status == SL_STATUS_OK, status, ChipLogError(DeviceLayer, "Failed to enable LI based sleep."));

    status = ConfigureBroadcastFilter(true);
    VerifyOrReturnError(status == SL_STATUS_OK, status, ChipLogError(DeviceLayer, "Failed to configure broadcasts filter."));

    return status;
}

sl_status_t WifiSleepManager::ConfigureDTIMBasedSleep()
{
    sl_status_t status = SL_STATUS_OK;

    status = ConfigurePowerSave(RSI_SLEEP_MODE_2, ASSOCIATED_POWER_SAVE, 0);
    VerifyOrReturnError(status == SL_STATUS_OK, status, ChipLogError(DeviceLayer, "Failed to enable to enable DTIM basedsleep."));

    status = ConfigureBroadcastFilter(false);
    VerifyOrReturnError(status == SL_STATUS_OK, status, ChipLogError(DeviceLayer, "Failed to configure broadcast filter."));

    return status;
}

sl_status_t WifiSleepManager::ConfigureHighPerformance()
{
    sl_status_t status = ConfigurePowerSave(RSI_ACTIVE, HIGH_PERFORMANCE, 0);
    if (status != SL_STATUS_OK)
    {
        ChipLogError(DeviceLayer, "Failed to set Wi-FI configuration to HighPerformance");
    }

    return status;
}
#endif // SLI_SI917

} // namespace Silabs
} // namespace DeviceLayer
} // namespace chip
