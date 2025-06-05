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
#include <platform/silabs/wifi/icd/WifiSleepManager.h>

<<<<<<< HEAD
using namespace chip::DeviceLayer::Silabs;
=======
namespace {

#if SLI_SI917 // 917 SoC & NCP

/**
 * @brief Configures the Wi-Fi Chip to go to LI based sleep.
 *        Function sets the listen interval the ICD Transort Slow Poll configuration and enables the broadcast filter.
 *
 * @return CHIP_ERROR CHIP_NO_ERROR if the configuration of the Wi-Fi chip was successful; otherwise MATTER_PLATFORM_ERROR
 *         with the sl_status_t error code from the Wi-Fi driver.
 */
CHIP_ERROR ConfigureLIBasedSleep()
{
    sl_status_t status = ConfigureBroadcastFilter(true);
    VerifyOrReturnError(status == SL_STATUS_OK, MATTER_PLATFORM_ERROR(status),
                        ChipLogError(DeviceLayer, "Failed to configure broadcasts filter."));

    // Allowing the device to go to sleep must be the last actions to avoid configuration failures.
    status = ConfigurePowerSave(RSI_SLEEP_MODE_2, ASSOCIATED_POWER_SAVE,
                                chip::ICDConfigurationData::GetInstance().GetSlowPollingInterval().count());
    VerifyOrReturnError(status == SL_STATUS_OK, MATTER_PLATFORM_ERROR(status),
                        ChipLogError(DeviceLayer, "Failed to enable LI based sleep."));

    return CHIP_NO_ERROR;
}

/**
 * @brief Configures the Wi-Fi Chip to go to DTIM based sleep.
 *        Function sets the listen interval to be synced with the DTIM beacon and disables the broadcast filter.
 *
 * @return CHIP_ERROR CHIP_NO_ERROR if the configuration of the Wi-Fi chip was successful; otherwise MATTER_PLATFORM_ERROR
 *         with the sl_status_t error code from the Wi-Fi driver.
 */
CHIP_ERROR ConfigureDTIMBasedSleep()
{
    sl_status_t status = ConfigureBroadcastFilter(false);
    VerifyOrReturnError(status == SL_STATUS_OK, MATTER_PLATFORM_ERROR(status),
                        ChipLogError(DeviceLayer, "Failed to configure broadcasts filter."));

    // Allowing the device to go to sleep must be the last actions to avoid configuration failures.
    status = ConfigurePowerSave(RSI_SLEEP_MODE_2, ASSOCIATED_POWER_SAVE, 0);
    VerifyOrReturnError(status == SL_STATUS_OK, MATTER_PLATFORM_ERROR(status),
                        ChipLogError(DeviceLayer, "Failed to enable DTIM based sleep."));

    return CHIP_NO_ERROR;
}

/**
 * @brief Configures the Wi-Fi chip to go Deep Sleep.
 *        Function doesn't change the state of the broadcast filter.
 *
 * @return CHIP_ERROR CHIP_NO_ERROR if the configuration of the Wi-Fi chip was successful; otherwise MATTER_PLATFORM_ERROR
 *         with the sl_status_t error code from the Wi-Fi driver.
 */
CHIP_ERROR ConfigureDeepSleep()
{
    sl_status_t status = ConfigurePowerSave(RSI_SLEEP_MODE_2, DEEP_SLEEP_WITH_RAM_RETENTION, 0);
    VerifyOrReturnError(status == SL_STATUS_OK, MATTER_PLATFORM_ERROR(status),
                        ChipLogError(DeviceLayer, "Failed to set Wi-FI configuration to DeepSleep."));
    return CHIP_NO_ERROR;
}

/**
 * @brief Configures the Wi-Fi chip to go to High Performance.
 *        Function doesn't change the broad cast filter configuration.
 *
 * @return CHIP_ERROR CHIP_NO_ERROR if the configuration of the Wi-Fi chip was successful; otherwise MATTER_PLATFORM_ERROR
 *         with the sl_status_t error code from the Wi-Fi driver.
 */
CHIP_ERROR ConfigureHighPerformance()
{
    sl_status_t status = ConfigurePowerSave(RSI_ACTIVE, HIGH_PERFORMANCE, 0);
    VerifyOrReturnError(status == SL_STATUS_OK, MATTER_PLATFORM_ERROR(status),
                        ChipLogError(DeviceLayer, "Failed to set Wi-FI configuration to HighPerformance."));
    return CHIP_NO_ERROR;
}
#endif // SLI_SI917

} // namespace
>>>>>>> afa58fe414 ([SL-UP] Replace CHIP_ERROR_INTERNAL with meaningful platform error code (#486))

namespace chip {
namespace DeviceLayer {
namespace Silabs {

// Initialize the static instance
WifiSleepManager WifiSleepManager::mInstance;

CHIP_ERROR WifiSleepManager::Init(PowerSaveInterface * platformInterface, WifiStateProvider * wifiStateProvider)
{
    VerifyOrReturnError(platformInterface != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(wifiStateProvider != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    mPowerSaveInterface = platformInterface;
    mWifiStateProvider  = wifiStateProvider;

    return VerifyAndTransitionToLowPowerMode(PowerEvent::kGenericEvent);
}

CHIP_ERROR WifiSleepManager::RequestHighPerformance(bool triggerTransition)
{
    VerifyOrReturnError(mHighPerformanceRequestCounter < std::numeric_limits<uint8_t>::max(), CHIP_ERROR_INTERNAL,
                        ChipLogError(DeviceLayer, "High performance request counter overflow"));

    mHighPerformanceRequestCounter++;

    if (triggerTransition)
    {
        // We don't do the mHighPerformanceRequestCounter check here; the check is in the VerifyAndTransitionToLowPowerMode function
        ReturnErrorOnFailure(VerifyAndTransitionToLowPowerMode(PowerEvent::kGenericEvent));
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR WifiSleepManager::RemoveHighPerformanceRequest()
{
    VerifyOrReturnError(mHighPerformanceRequestCounter > 0, CHIP_NO_ERROR,
                        ChipLogError(DeviceLayer, "Wi-Fi configuration already in low power mode"));

    mHighPerformanceRequestCounter--;

    // We don't do the mHighPerformanceRequestCounter check here; the check is in the VerifyAndTransitionToLowPowerMode function
    ReturnErrorOnFailure(VerifyAndTransitionToLowPowerMode(PowerEvent::kGenericEvent));

    return CHIP_NO_ERROR;
}

CHIP_ERROR WifiSleepManager::HandlePowerEvent(PowerEvent event)
{
    switch (event)
    {
    case PowerEvent::kCommissioningComplete:
        ChipLogProgress(AppServer, "WifiSleepManager: Handling Commissioning Complete Event");
        mIsCommissioningInProgress = false;

        // TODO: Remove High Performance Req during commissioning when sleep issues are resolved
        WifiSleepManager::GetInstance().RemoveHighPerformanceRequest();
        break;

    case PowerEvent::kConnectivityChange:
    case PowerEvent::kGenericEvent:
        // No additional processing needed for these events at the moment
        break;

    default:
        ChipLogError(AppServer, "Unknown Power Event");
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR WifiSleepManager::VerifyAndTransitionToLowPowerMode(PowerEvent event)
{
    VerifyOrDieWithMsg(mWifiStateProvider != nullptr, DeviceLayer, "WifiStateProvider is not initialized");
    VerifyOrDieWithMsg(mPowerSaveInterface != nullptr, DeviceLayer, "PowerSaveInterface is not initialized");

    ReturnErrorOnFailure(HandlePowerEvent(event));

    if (mHighPerformanceRequestCounter > 0)
    {
        return ConfigureHighPerformance();
    }

    if (mIsCommissioningInProgress)
    {
        // During commissioning, don't let the device go to sleep
        // This is needed to interrupt the sleep and retry joining the network
        return CHIP_NO_ERROR;
    }

    if (!mWifiStateProvider->IsWifiProvisioned())
    {
        return ConfigureDeepSleep();
    }

    if (mCallback && mCallback->CanGoToLIBasedSleep())
    {
        return ConfigureLIBasedSleep();
    }

    return ConfigureDTIMBasedSleep();
}

CHIP_ERROR WifiSleepManager::ConfigureDTIMBasedSleep()
{
    VerifyOrDieWithMsg(mPowerSaveInterface != nullptr, DeviceLayer, "PowerSaveInterface is not initialized");

    ReturnLogErrorOnFailure(mPowerSaveInterface->ConfigureBroadcastFilter(false));

    // Allowing the device to go to sleep must be the last actions to avoid configuration failures.
    ReturnLogErrorOnFailure(
        mPowerSaveInterface->ConfigurePowerSave(PowerSaveInterface::PowerSaveConfiguration::kConnectedSleep, 0));

<<<<<<< HEAD
=======
#elif RS911X_WIFI // rs9116
    sl_status_t status = ConfigurePowerSave();
    VerifyOrReturnError(status == SL_STATUS_OK, MATTER_PLATFORM_ERROR(status));
>>>>>>> afa58fe414 ([SL-UP] Replace CHIP_ERROR_INTERNAL with meaningful platform error code (#486))
    return CHIP_NO_ERROR;
}

CHIP_ERROR WifiSleepManager::ConfigureDeepSleep()
{
    VerifyOrDieWithMsg(mPowerSaveInterface != nullptr, DeviceLayer, "PowerSaveInterface is not initialized");

    ReturnLogErrorOnFailure(mPowerSaveInterface->ConfigurePowerSave(PowerSaveInterface::PowerSaveConfiguration::kDeepSleep, 0));
    return CHIP_NO_ERROR;
}

CHIP_ERROR WifiSleepManager::ConfigureHighPerformance()
{
    VerifyOrDieWithMsg(mPowerSaveInterface != nullptr, DeviceLayer, "PowerSaveInterface is not initialized");

    ReturnLogErrorOnFailure(
        mPowerSaveInterface->ConfigurePowerSave(PowerSaveInterface::PowerSaveConfiguration::kHighPerformance, 0));
    return CHIP_NO_ERROR;
}

CHIP_ERROR WifiSleepManager::ConfigureLIBasedSleep()
{
    ReturnLogErrorOnFailure(mPowerSaveInterface->ConfigureBroadcastFilter(true));

    // Allowing the device to go to sleep must be the last actions to avoid configuration failures.
    ReturnLogErrorOnFailure(
        mPowerSaveInterface->ConfigurePowerSave(PowerSaveInterface::PowerSaveConfiguration::kLIConnectedSleep,
                                                chip::ICDConfigurationData::GetInstance().GetSlowPollingInterval().count()));

    return CHIP_NO_ERROR;
}

} // namespace Silabs
} // namespace DeviceLayer
} // namespace chip
