#include "SleepManager.h"
#include <lib/support/logging/CHIPLogging.h>
#include <platform/silabs/wifi/WifiInterfaceAbstraction.h>

using namespace chip::app;

namespace chip {
namespace DeviceLayer {
namespace Silabs {

// Initialize the static instance
SleepManager SleepManager::mInstance;

CHIP_ERROR SleepManager::Init()
{
    VerifyOrReturnError(mIMEngine != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(mFabricTable != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    mIMEngine->RegisterReadHandlerAppCallback(this);
    mFabricTable->AddFabricDelegate(this);

    PlatformMgr().AddEventHandler(OnPlatformEvent, reinterpret_cast<intptr_t>(this));

    return CHIP_NO_ERROR;
}

void SleepManager::OnSubscriptionEstablished(ReadHandler & aReadHandler)
{
    // Implement logic for when a subscription is established
}

void SleepManager::OnSubscriptionTerminated(ReadHandler & aReadHandler)
{
    // Implement logic for when a subscription is terminated
}

void SleepManager::OnFabricRemoved(const chip::FabricTable & fabricTable, chip::FabricIndex fabricIndex)
{
    // Implement logic for when a fabric is removed
}

void SleepManager::OnFabricCommitted(const chip::FabricTable & fabricTable, chip::FabricIndex fabricIndex)
{
    // Implement logic for when a fabric is committed
}

void SleepManager::OnEnterActiveMode()
{
    // Execution logic for entering active mode
}

void SleepManager::OnEnterIdleMode()
{
    // Execution logic for entering idle mode
}

void SleepManager::HandleCommissioningComplete()
{
    if (wfx_power_save(RSI_SLEEP_MODE_2, ASSOCIATED_POWER_SAVE) != SL_STATUS_OK)
    {
        ChipLogError(AppServer, "wfx_power_save failed");
    }
}

void SleepManager::HandleInternetConnectivityChange(const ChipDeviceEvent * event)
{
    if (event->InternetConnectivityChange.IPv6 == kConnectivity_Established)
    {
        if (!IsCommissioningInProgress())
        {
            if (wfx_power_save(RSI_SLEEP_MODE_2, ASSOCIATED_POWER_SAVE) != SL_STATUS_OK)
            {
                ChipLogError(AppServer, "wfx_power_save failed");
            }
        }
    }
}

void SleepManager::HandleCommissioningWindowClose()
{
    if (!ConnectivityMgr().IsWiFiStationProvisioned() && !IsCommissioningInProgress())
    {
        if (wfx_power_save(RSI_SLEEP_MODE_8, DEEP_SLEEP_WITH_RAM_RETENTION) != SL_STATUS_OK)
        {
            ChipLogError(AppServer, "Failed to enable the TA Deep Sleep");
        }
    }
}

void SleepManager::HandleCommissioningSessionStarted()
{
    SetCommissioningInProgress(true);
}

void SleepManager::HandleCommissioningSessionStopped()
{
    SetCommissioningInProgress(false);
}

void SleepManager::OnPlatformEvent(const chip::DeviceLayer::ChipDeviceEvent * event, intptr_t arg)
{
    SleepManager * manager = reinterpret_cast<SleepManager *>(arg);
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

} // namespace Silabs
} // namespace DeviceLayer
} // namespace chip
