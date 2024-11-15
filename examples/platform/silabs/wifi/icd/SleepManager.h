#pragma once

#include <app/InteractionModelEngine.h>
#include <app/ReadHandler.h>
#include <app/icd/server/ICDManager.h>
#include <app/icd/server/ICDStateObserver.h>
#include <credentials/FabricTable.h>
#include <lib/core/CHIPError.h>

namespace chip {
namespace DeviceLayer {
namespace Silabs {

/**
 * @brief SleepManager is a singleton class that manages the sleep modes for Wi-Fi devices.
 *        The class contains the buisness logic associated with optimizing the sleep states based on the Matter SDK internal states
 *
 *        The class implements two disctint optimization states; one of SIT devices and one of LIT devices
 *        For SIT ICDs, the logic is based on the Subscriptions established with the device.
 *        For LIT ICDs, the logic is based on the ICDManager operating modes. The LIT mode also utilizes the SIT mode logic.
 */
class SleepManager : public chip::app::ICDStateObserver,
                     public chip::app::ReadHandler::ApplicationCallback,
                     public chip::FabricTable::Delegate
{
public:
    SleepManager(const SleepManager &)             = delete;
    SleepManager & operator=(const SleepManager &) = delete;

    static SleepManager & GetInstance() { return mInstance; }

    /**
     * @brief Init function that configure the SleepManager APIs based on the type of ICD
     *        SIT ICD: Init function registers the ReadHandler Application callback to be notified when a subscription is
     *                 established or destroyed.
     *
     *        LIT ICD: Init function registers with the ICDManager as an observer to be notified of the ICD mode changes.
     *
     * @return CHIP_ERROR
     */
    CHIP_ERROR Init();

    SleepManager & SetInteractionModelEngine(chip::app::InteractionModelEngine * engine)
    {
        mIMEngine = engine;
        return *this;
    }

    SleepManager & SetFabricTable(chip::FabricTable * fabricTable)
    {
        mFabricTable = fabricTable;
        return *this;
    }

    // ICDStateObserver implementation overrides

    void OnEnterActiveMode();
    void OnEnterIdleMode();
    void OnTransitionToIdle()
    { /* No execution logic */
    }
    void OnICDModeChange()
    { /* No execution logic */
    }

    // ReadHandler::ApplicationCallback implementation overrides

    void OnSubscriptionEstablished(chip::app::ReadHandler & aReadHandler);
    void OnSubscriptionTerminated(chip::app::ReadHandler & aReadHandler);

    // FabricTable::Delegate implementation overrides
    void FabricWillBeRemoved(const chip::FabricTable & fabricTable,
                             chip::FabricIndex fabricIndex) override{ /* No execution logic */ };
    void OnFabricRemoved(const chip::FabricTable & fabricTable, chip::FabricIndex fabricIndex) override;
    void OnFabricCommitted(const chip::FabricTable & fabricTable, chip::FabricIndex fabricIndex) override;
    void OnFabricUpdated(const chip::FabricTable & fabricTable, chip::FabricIndex fabricIndex) override{ /* No execution logic*/ };

    static void OnPlatformEvent(const chip::DeviceLayer::ChipDeviceEvent * event, intptr_t arg);

    /**
     * @brief Set the commissioning status of the device
     *
     * @param[in] inProgress bool true if commissioning is in progress, false otherwise
     */
    void SetCommissioningInProgress(bool inProgress) { isCommissioningInProgress = inProgress; }

    /**
     * @brief Returns the current commissioning status of the device
     *
     * @return bool true if commissioning is in progress, false otherwise
     */
    bool IsCommissioningInProgress() { return isCommissioningInProgress; }

private:
    SleepManager()  = default;
    ~SleepManager() = default;

    void HandleCommissioningComplete();
    void HandleInternetConnectivityChange(const chip::DeviceLayer::ChipDeviceEvent * event);
    void HandleCommissioningWindowClose();
    void HandleCommissioningSessionStarted();
    void HandleCommissioningSessionStopped();

    static SleepManager mInstance;
    chip::app::InteractionModelEngine * mIMEngine = nullptr;
    chip::FabricTable * mFabricTable              = nullptr;
    bool isCommissioningInProgress                = false;
};

} // namespace Silabs
} // namespace DeviceLayer
} // namespace chip
