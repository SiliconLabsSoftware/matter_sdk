#include "SleepManager.h"
#include <app/icd/server/ICDConfigurationData.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/silabs/wifi/WifiInterfaceAbstraction.h>

using namespace chip::app;

namespace chip {
namespace DeviceLayer {
namespace Silabs {

// Initialize the static instance
PlatformSleepManager PlatformSleepManager::mInstance;

CHIP_ERROR PlatformSleepManager::Init()
{
    PlatformMgr().AddEventHandler(OnPlatformEvent, reinterpret_cast<intptr_t>(this));
    return CHIP_NO_ERROR;
}

void PlatformSleepManager::HandleCommissioningComplete()
{
    if (wfx_power_save(RSI_SLEEP_MODE_2, ASSOCIATED_POWER_SAVE, 0) != SL_STATUS_OK)
    {
        ChipLogError(AppServer, "wfx_power_save failed");
    }
}

void PlatformSleepManager::HandleInternetConnectivityChange(const ChipDeviceEvent * event)
{
    if (event->InternetConnectivityChange.IPv6 == kConnectivity_Established)
    {
        if (!IsCommissioningInProgress())
        {
            if
            {
                ChipLogError(AppServer, "wfx_power_save failed");
            }
        }
    }
}

void PlatformSleepManager::HandleCommissioningWindowClose() {}

void PlatformSleepManager::HandleCommissioningSessionStarted()
{
    SetCommissioningInProgress(true);
}

void PlatformSleepManager::HandleCommissioningSessionStopped()
{
    SetCommissioningInProgress(false);
}

void PlatformSleepManager::OnPlatformEvent(const chip::DeviceLayer::ChipDeviceEvent * event, intptr_t arg)
{
    PlatformSleepManager * manager = reinterpret_cast<PlatformSleepManager *>(arg);
    VerifyOrDie(manager != nullptr);

    switch (event->Type)
    {
    case DeviceEventType::kCommissioningComplete:
        manager->HandleCommissioningComplete();
        break;

    case DeviceEventType::kInternetConnectivityChange:
        manager->HandleInternetConnectivityChange(event);
        break;

    case DeviceEventType::kCommissioningWindowClose:
        manager->HandleCommissioningWindowClose();
        break;

    case DeviceEventType::kCommissioningSessionStarted:
        manager->HandleCommissioningSessionStarted();
        break;

    case DeviceEventType::kCommissioningSessionStopped:
        manager->HandleCommissioningSessionStopped();
        break;

    default:
        break;
    }
}

CHIP_ERROR PlatformSleepManager::RequestHighPerformance()
{
    VerifyOrReturnError(mRequestHighPerformanceCounter < std::numeric_limits<uint8_t>::max(), CHIP_ERROR_INTERNAL,
                        ChipLogError(DeviceLayer, "High performance request counter overflow"));

    if (mRequestHighPerformanceCounter == 0)
    {
        VerifyOrReturnError(wfx_power_save(RSI_ACTIVE, HIGH_PERFORMANCE, 0) == SL_STATUS_OK, CHIP_ERROR_INTERNAL,
                            ChipLogError(DeviceLayer, "Failed to set Wi-FI configuration to HighPerformance"));
        VerifyOrReturnError(ConfigureBroadcastFilter(false) == SL_STATUS_OK, CHIP_ERROR_INTERNAL,
                            ChipLogError(DeviceLayer, "Failed to disable broadcast filter"));
    }

    mRequestHighPerformanceCounter++;
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

    if (!ConnectivityMgr().IsWiFiStationProvisioned() && !IsCommissioningInProgress())
    {
        VerifyOrReturnError(wfx_power_save(RSI_SLEEP_MODE_8, DEEP_SLEEP_WITH_RAM_RETENTION, 0) != SL_STATUS_OK,
                            ChipLogError(DeviceLayer, "Failed to enable the TA Deep Sleep");)
    }
    else
    {
        VerifyOrReturnError(wfx_power_save(RSI_SLEEP_MODE_2, ASSOCIATED_POWER_SAVE, 0) != SL_STATUS_OK,
                            ChipLogError(DeviceLayer, "Failed to enable the TA Deep Sleep");)
    }

    return CHIP_NO_ERROR;
}

} // namespace Silabs
} // namespace DeviceLayer
} // namespace chip
