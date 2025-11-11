#pragma once

#include <cstddef>
#include <cstdlib>
#include <lib/core/CHIPError.h>
#include <system/SystemClock.h>

#include "cmsis_os2.h"
#include <sl_power_manager.h>

namespace chip {
namespace Tracing {
namespace Silabs {

struct EnergyTrace
{
    System::Clock::Milliseconds32 mEntryTime;
    sl_power_manager_em_t mEnergyMode;
};

class SilabsPowerTracing
{
public:
    SilabsPowerTracing();
    ~SilabsPowerTracing();

    static SilabsPowerTracing & Instance() { return sInstance; }

    CHIP_ERROR Init();
    CHIP_ERROR OutputPowerManagerTraces();

    /** @brief Check if the power tracing system is initialized
     *  @return true if initialized, false otherwise
     */
    bool IsInitialized() const { return mInitialized; }

    /** @brief Callback for power manager energy mode transitions
     * This function is called by the power manager when the device transitions between energy modes.
     * It updates the time spent in each energy mode.
     *  @param from The energy mode the device is transitioning from
     *  @param to The energy mode the device is transitioning to
     */
    void PowerManagerTransitionCallback(sl_power_manager_em_t from, sl_power_manager_em_t to);

    /** @brief Static callback for power manager energy mode transitions
     * This function is a static wrapper that calls the instance method PowerManagerTransitionCallback.
     *  @param from The energy mode the device is transitioning from
     *  @param to The energy mode the device is transitioning to
     */
    static void StaticPowerManagerTransitionCallback(sl_power_manager_em_t from, sl_power_manager_em_t to);

private:
    static SilabsPowerTracing sInstance;

    // Energy trace storage
    EnergyTrace * mEnergyTraces;
    size_t mEnergyTraceCount;

    // Power manager event handling
    sl_power_manager_em_transition_event_handle_t mPowerManagerEmTransitionEventHandle;
    sl_power_manager_em_transition_event_info_t mPowerManagerEmTransitionEventInfo;

    // Statistics timer
    osTimerId_t mStatisticsTimer;

    // Initialization state
    bool mInitialized;
};

} // namespace Silabs
} // namespace Tracing
} // namespace chip
