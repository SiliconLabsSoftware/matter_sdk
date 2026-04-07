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

#if CHIP_CONFIG_ENABLE_ICD_LIT
#include <cmsis_os2.h>
#include <platform/PlatformManager.h>
#endif // CHIP_CONFIG_ENABLE_ICD_LIT

using namespace chip::DeviceLayer::Silabs;

#if CHIP_CONFIG_ENABLE_ICD_LIT
namespace {
constexpr uint32_t kLitPrecheckInMarginSeconds = 10;
osTimerId_t sLitPrecheckReconnectTimer         = nullptr;
} // namespace
#endif // CHIP_CONFIG_ENABLE_ICD_LIT

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

#if CHIP_CONFIG_ENABLE_ICD_LIT
    if (sLitPrecheckReconnectTimer == nullptr)
    {
        sLitPrecheckReconnectTimer = osTimerNew(LitPrecheckReconnectTimerHandler, osTimerOnce, nullptr, nullptr);
        VerifyOrReturnError(sLitPrecheckReconnectTimer != nullptr, CHIP_ERROR_NO_MEMORY);
    }
#endif // CHIP_CONFIG_ENABLE_ICD_LIT

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
        TEMPORARY_RETURN_IGNORED WifiSleepManager::GetInstance().RemoveHighPerformanceRequest();
        break;

    case PowerEvent::kConnectivityChange:
    case PowerEvent::kGenericEvent:
    case PowerEvent::kActiveMode:
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

#if CHIP_CONFIG_ENABLE_ICD_LIT
    if (event == PowerEvent::kActiveMode)
    {
        if (mWifiStateProvider->IsWifiProvisioned() && !mWifiStateProvider->IsStationConnected())
        {
            return ConfigureLITConnect();
        }
    }
#endif // CHIP_CONFIG_ENABLE_ICD_LIT

    if (!mWifiStateProvider->IsWifiProvisioned())
    {
        return ConfigureDeepSleep();
    }

    if (mCallback && mCallback->CanGoToLIBasedSleep())
    {
#if CHIP_CONFIG_ENABLE_ICD_LIT
        return ConfigureLITDisconnect();
#else
        return ConfigureLIBasedSleep();
#endif
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

#if CHIP_CONFIG_ENABLE_ICD_LIT
CHIP_ERROR WifiSleepManager::ConfigureLITConnect()
{
    VerifyOrDieWithMsg(mPowerSaveInterface != nullptr, DeviceLayer, "PowerSaveInterface is not initialized");
    return mPowerSaveInterface->ConfigureLITConnect();
}

CHIP_ERROR WifiSleepManager::ConfigureLITDisconnect()
{
    VerifyOrDieWithMsg(mPowerSaveInterface != nullptr, DeviceLayer, "PowerSaveInterface is not initialized");
    ReturnLogErrorOnFailure(mPowerSaveInterface->ConfigureLITDisconnect());
    ReturnLogErrorOnFailure(mPowerSaveInterface->ConfigureBroadcastFilter(true));
    ReturnLogErrorOnFailure(
        mPowerSaveInterface->ConfigurePowerSave(PowerSaveInterface::PowerSaveConfiguration::kLITDisconnectSleep,
                                                chip::ICDConfigurationData::GetInstance().GetSlowPollingInterval().count()));

    StartLITConnectTimer();
    return CHIP_NO_ERROR;
}

void WifiSleepManager::StartLITConnectTimer()
{
    VerifyOrReturn(sLitPrecheckReconnectTimer != nullptr, ChipLogError(DeviceLayer, "StartLITConnectTimer: timer not initialized"));

    const uint32_t idleSec =
        static_cast<uint32_t>(chip::ICDConfigurationData::GetInstance().GetModeBasedIdleModeDuration().count());
    const uint32_t activeSec = chip::ICDConfigurationData::GetInstance().GetActiveModeThreshold().count() / 1000;
    const uint32_t delaySec  = (idleSec > kLitPrecheckInMarginSeconds) ? (idleSec - activeSec - kLitPrecheckInMarginSeconds) : 1u;
    const uint32_t delayMs   = delaySec * 1000u;

    if(osTimerIsRunning(sLitPrecheckReconnectTimer))
    {
        (void) osTimerStop(sLitPrecheckReconnectTimer);
    }

    if (osTimerStart(sLitPrecheckReconnectTimer, pdMS_TO_TICKS(delayMs)) != osOK)
    {
        ChipLogError(DeviceLayer, "StartLITConnectTimer: osTimerStart failed");
    }
}

void WifiSleepManager::LitPrecheckReconnectTimerHandler(void * arg)
{
    (void) arg;
    TEMPORARY_RETURN_IGNORED WifiSleepManager::GetInstance().ConfigureLITConnect();
}
#endif // CHIP_CONFIG_ENABLE_ICD_LIT

} // namespace Silabs
} // namespace DeviceLayer
} // namespace chip
