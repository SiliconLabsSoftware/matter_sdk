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
<<<<<<< HEAD
#include <platform/silabs/wifi/WifiInterfaceAbstraction.h>
#include <platform/silabs/wifi/icd/WifiSleepManager.h>

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

=======
#include <platform/silabs/wifi/icd/WifiSleepManager.h>

>>>>>>> csa/v1.4.2-branch
namespace chip {
namespace DeviceLayer {
namespace Silabs {

// Initialize the static instance
WifiSleepManager WifiSleepManager::mInstance;

<<<<<<< HEAD
CHIP_ERROR WifiSleepManager::Init()
{
    return CHIP_NO_ERROR;
}

CHIP_ERROR WifiSleepManager::RequestHighPerformance()
=======
CHIP_ERROR WifiSleepManager::Init(PowerSaveInterface * platformInterface, WifiStateProvider * wifiStateProvider)
{
    VerifyOrReturnError(platformInterface != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(wifiStateProvider != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    mPowerSaveInterface = platformInterface;
    mWifiStateProvider  = wifiStateProvider;

    return VerifyAndTransitionToLowPowerMode(PowerEvent::kGenericEvent);
}

CHIP_ERROR WifiSleepManager::RequestHighPerformance(bool triggerTransition)
>>>>>>> csa/v1.4.2-branch
{
    VerifyOrReturnError(mHighPerformanceRequestCounter < std::numeric_limits<uint8_t>::max(), CHIP_ERROR_INTERNAL,
                        ChipLogError(DeviceLayer, "High performance request counter overflow"));

    mHighPerformanceRequestCounter++;

<<<<<<< HEAD
    // We don't do the mHighPerformanceRequestCounter check here; the check is in VerifyAndTransitionToLowPowerMode function
    ReturnErrorOnFailure(VerifyAndTransitionToLowPowerMode());
=======
    if (triggerTransition)
    {
        // We don't do the mHighPerformanceRequestCounter check here; the check is in the VerifyAndTransitionToLowPowerMode function
        ReturnErrorOnFailure(VerifyAndTransitionToLowPowerMode(PowerEvent::kGenericEvent));
    }
>>>>>>> csa/v1.4.2-branch

    return CHIP_NO_ERROR;
}

CHIP_ERROR WifiSleepManager::RemoveHighPerformanceRequest()
{
    VerifyOrReturnError(mHighPerformanceRequestCounter > 0, CHIP_NO_ERROR,
                        ChipLogError(DeviceLayer, "Wi-Fi configuration already in low power mode"));

    mHighPerformanceRequestCounter--;

<<<<<<< HEAD
    // We don't do the mHighPerformanceRequestCounter check here; the check is in VerifyAndTransitionToLowPowerMode function
    ReturnErrorOnFailure(VerifyAndTransitionToLowPowerMode());
=======
    // We don't do the mHighPerformanceRequestCounter check here; the check is in the VerifyAndTransitionToLowPowerMode function
    ReturnErrorOnFailure(VerifyAndTransitionToLowPowerMode(PowerEvent::kGenericEvent));
>>>>>>> csa/v1.4.2-branch

    return CHIP_NO_ERROR;
}

CHIP_ERROR WifiSleepManager::HandlePowerEvent(PowerEvent event)
{
    switch (event)
    {
    case PowerEvent::kCommissioningComplete:
        ChipLogProgress(AppServer, "WifiSleepManager: Handling Commissioning Complete Event");
        mIsCommissioningInProgress = false;
<<<<<<< HEAD
        break;
=======

        // TODO: Remove High Performance Req during commissioning when sleep issues are resolved
        WifiSleepManager::GetInstance().RemoveHighPerformanceRequest();
        break;

>>>>>>> csa/v1.4.2-branch
    case PowerEvent::kConnectivityChange:
    case PowerEvent::kGenericEvent:
        // No additional processing needed for these events at the moment
        break;
<<<<<<< HEAD
=======

>>>>>>> csa/v1.4.2-branch
    default:
        ChipLogError(AppServer, "Unknown Power Event");
        return CHIP_ERROR_INVALID_ARGUMENT;
    }
<<<<<<< HEAD
=======

>>>>>>> csa/v1.4.2-branch
    return CHIP_NO_ERROR;
}

CHIP_ERROR WifiSleepManager::VerifyAndTransitionToLowPowerMode(PowerEvent event)
{
<<<<<<< HEAD
    ReturnErrorOnFailure(HandlePowerEvent(event));

#if SLI_SI917 // 917 SoC & NCP
=======
    VerifyOrDieWithMsg(mWifiStateProvider != nullptr, DeviceLayer, "WifiStateProvider is not initialized");
    VerifyOrDieWithMsg(mPowerSaveInterface != nullptr, DeviceLayer, "PowerSaveInterface is not initialized");

    ReturnErrorOnFailure(HandlePowerEvent(event));

>>>>>>> csa/v1.4.2-branch
    if (mHighPerformanceRequestCounter > 0)
    {
        return ConfigureHighPerformance();
    }

    if (mIsCommissioningInProgress)
    {
<<<<<<< HEAD
        // During commissioning, don't let the device to go to sleep
        // This is needed to interrupt the sleep and retry to join the network
        return CHIP_NO_ERROR;
    }

    // TODO: Clean this up when the Wi-Fi interface re-work is finished
    wfx_wifi_provision_t wifiConfig;
    wfx_get_wifi_provision(&wifiConfig);

    if (!(wifiConfig.ssid[0] != 0))
=======
        // During commissioning, don't let the device go to sleep
        // This is needed to interrupt the sleep and retry joining the network
        return CHIP_NO_ERROR;
    }

    if (!mWifiStateProvider->IsWifiProvisioned())
>>>>>>> csa/v1.4.2-branch
    {
        return ConfigureDeepSleep();
    }

<<<<<<< HEAD
    if (mCallback && mCallback->CanGoToLIBasedSleep())
    {
        return ConfigureLIBasedSleep();
    }

    return ConfigureDTIMBasedSleep();

#elif RS911X_WIFI // rs9116
    sl_status_t status = ConfigurePowerSave();
    VerifyOrReturnError(status == SL_STATUS_OK, MATTER_PLATFORM_ERROR(status));
    return CHIP_NO_ERROR;
#else             // wf200
    return CHIP_NO_ERROR;
#endif
=======
    return ConfigureDTIMBasedSleep();
}

CHIP_ERROR WifiSleepManager::ConfigureDTIMBasedSleep()
{
    VerifyOrDieWithMsg(mPowerSaveInterface != nullptr, DeviceLayer, "PowerSaveInterface is not initialized");

    ReturnLogErrorOnFailure(mPowerSaveInterface->ConfigureBroadcastFilter(false));

    // Allowing the device to go to sleep must be the last actions to avoid configuration failures.
    ReturnLogErrorOnFailure(
        mPowerSaveInterface->ConfigurePowerSave(PowerSaveInterface::PowerSaveConfiguration::kConnectedSleep, 0));

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
>>>>>>> csa/v1.4.2-branch
}

} // namespace Silabs
} // namespace DeviceLayer
} // namespace chip
