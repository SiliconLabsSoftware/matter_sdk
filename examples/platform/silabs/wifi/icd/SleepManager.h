
#pragma once

#include <app/ReadHandler.h>
#include <app/icd/server/ICDManager.h>
#include <app/icd/server/ICDStateObserver.h>
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
class SleepManager : public chip::app::ICDStateObserver, public chip::app::ReadHandler::ApplicationCallback
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

    // ICDStateObserver implementation overrides

    void OnEnterActiveMode();
    void OnEnterIdleMode();
    void OnTransitionToIdle() { /* No execution logic */ }
    void OnICDModeChange() { /* No execution logic */ }

    // ReadHandler::ApplicationCallback implementation overrides

    void OnSubscriptionEstablished(chip::app::ReadHandler & aReadHandler);
    void OnSubscriptionTerminated(chip::app::ReadHandler & aReadHandler);

private:
    SleepManager()  = default;
    ~SleepManager() = default;

    static SleepManager mInstance;
};

} // namespace Silabs
} // namespace DeviceLayer
} // namespace chip
