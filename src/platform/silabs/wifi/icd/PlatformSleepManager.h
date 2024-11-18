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
 * @brief PlatformSleepManager is a singleton class that manages the sleep modes for Wi-Fi devices.
 *        The class contains the buisness logic associated with optimizing the sleep states based on the Matter SDK internal states
 */
class PlatformSleepManager
{
public:
    PlatformSleepManager(const PlatformSleepManager &)             = delete;
    PlatformSleepManager & operator=(const PlatformSleepManager &) = delete;

    static PlatformSleepManager & GetInstance() { return mInstance; }

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

    static void OnPlatformEvent(const chip::DeviceLayer::ChipDeviceEvent * event, intptr_t arg);

private:
    PlatformSleepManager()  = default;
    ~PlatformSleepManager() = default;

    void HandleCommissioningComplete();
    void HandleInternetConnectivityChange(const chip::DeviceLayer::ChipDeviceEvent * event);
    void HandleCommissioningWindowClose();
    void HandleCommissioningSessionStarted();
    void HandleCommissioningSessionStopped();

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

    /**
     * @brief Transition the device to the Lowest Power State.
     *        The function is responsible of deciding if the device can go to a LI based sleep
     *        or is required to stay in a DTIM based sleep to be able to receive mDNS messages
     *
     * @return CHIP_ERROR CHIP_NO_ERROR if the device was transitionned to low power
     *         CHIP_ERROR_INTERNAL if an error occured
     */
    CHIP_ERROR TransitionToLowPowerMode();

    static PlatformSleepManager mInstance;
    bool isCommissioningInProgress = false;

    uint8_t mHighPerformanceRequestCounter = 0;
};

} // namespace Silabs
} // namespace DeviceLayer
} // namespace chip
