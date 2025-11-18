#include "SilabsPowerTracing.h"
#include <lib/support/CodeUtils.h>
#include <platform/silabs/tracing/SilabsTracingConfig.h>

// Need to use the sleeptimer for power tracing. The RAIL timer doesn't tick in EM2 and creates invalid timestamps for this
// application.
#ifndef SILABS_GET_SLEEPTIMER_TIME
#if defined(SL_RAIL_LIB_MULTIPROTOCOL_SUPPORT) && SL_RAIL_LIB_MULTIPROTOCOL_SUPPORT
#include "sl_sleeptimer.h"
#define SILABS_GET_SLEEPTIMER_TIME() uint32_t((sl_sleeptimer_get_tick_count64() * 1000ULL) / sl_sleeptimer_get_timer_frequency())
#else
// For unit tests, this should be defined by the test file before including the implementation
// If not defined, provide a minimal default
#define SILABS_GET_SLEEPTIMER_TIME() 0
#endif
#endif // SILABS_GET_SLEEPTIMER_TIME

namespace chip {
namespace Tracing {
namespace Silabs {

// Timer callback function - defined before use
namespace {
void OnPowerManagerStatisticsTimer(void * argument)
{
    SilabsPowerTracing::Instance().OutputPowerManagerTraces();
}
} // namespace

// Define the static singleton instance
SilabsPowerTracing SilabsPowerTracing::sInstance;

// singleton instance constructor
SilabsPowerTracing::SilabsPowerTracing() :
    mEnergyTraces(nullptr), mEnergyTraceCount(0), mPowerManagerEmTransitionEventHandle(),
    mPowerManagerEmTransitionEventInfo{ (SL_POWER_MANAGER_EVENT_TRANSITION_ENTERING_EM0 |
                                         SL_POWER_MANAGER_EVENT_TRANSITION_ENTERING_EM1 |
                                         SL_POWER_MANAGER_EVENT_TRANSITION_ENTERING_EM2),
                                        StaticPowerManagerTransitionCallback },
    mStatisticsTimer(nullptr), mInitialized(false)
{}

CHIP_ERROR SilabsPowerTracing::Init()
{
    CHIP_ERROR err              = CHIP_NO_ERROR;
    bool powerManagerSubscribed = false;

    // Return early if already initialized
    if (mInitialized)
    {
        return CHIP_NO_ERROR;
    }

    // Allocate energy trace storage
    if (mEnergyTraces == nullptr)
    {
        mEnergyTraces = static_cast<EnergyTrace *>(calloc(SILABS_TRACING_ENERGY_TRACES_MAX, sizeof(EnergyTrace)));
        if (mEnergyTraces == nullptr)
        {
            err = CHIP_ERROR_NO_MEMORY;
        }
    }

    if (err == CHIP_NO_ERROR)
    {
        mEnergyTraceCount = 0;

        // Initialize power manager and subscribe to events
        sl_power_manager_init();
        sl_power_manager_subscribe_em_transition_event(&mPowerManagerEmTransitionEventHandle, &mPowerManagerEmTransitionEventInfo);
        powerManagerSubscribed = true;

        // Create and start one-shot timer for statistics output
        mStatisticsTimer = osTimerNew(OnPowerManagerStatisticsTimer, osTimerOnce, nullptr, nullptr);
        if (mStatisticsTimer == nullptr)
        {
            ChipLogError(DeviceLayer, "Failed to create power manager statistics timer");
            err = CHIP_ERROR_NO_MEMORY;
        }
    }

    if (err == CHIP_NO_ERROR)
    {
        uint32_t ticks = SILABS_TRACING_ENERGY_TRACES_SECONDS * osKernelGetTickFreq();
        if (osTimerStart(mStatisticsTimer, ticks) != osOK)
        {
            ChipLogError(DeviceLayer, "Failed to start power manager statistics timer");
            err = CHIP_ERROR_INTERNAL;
        }
    }

    // Cleanup on error
    if (err != CHIP_NO_ERROR)
    {
        if (mStatisticsTimer != nullptr)
        {
            osTimerDelete(mStatisticsTimer);
            mStatisticsTimer = nullptr;
        }

        if (powerManagerSubscribed)
        {
            sl_power_manager_unsubscribe_em_transition_event(&mPowerManagerEmTransitionEventHandle);
        }

        if (mEnergyTraces != nullptr)
        {
            free(mEnergyTraces);
            mEnergyTraces     = nullptr;
            mEnergyTraceCount = 0;
        }
    }
    else
    {
        mInitialized = true;
    }

    return err;
}

SilabsPowerTracing::~SilabsPowerTracing()
{
    if (mStatisticsTimer != nullptr)
    {
        osTimerStop(mStatisticsTimer);
        osTimerDelete(mStatisticsTimer);
        mStatisticsTimer = nullptr;
    }

    if (mEnergyTraces != nullptr)
    {
        free(mEnergyTraces);
        mEnergyTraces     = nullptr;
        mEnergyTraceCount = 0;
    }

    sl_power_manager_unsubscribe_em_transition_event(&mPowerManagerEmTransitionEventHandle);
    mInitialized = false;
}

void SilabsPowerTracing::PowerManagerTransitionCallback(sl_power_manager_em_t from, sl_power_manager_em_t to)
{
    if (mEnergyTraces != nullptr && mEnergyTraceCount < SILABS_TRACING_ENERGY_TRACES_MAX)
    {
        mEnergyTraces[mEnergyTraceCount].mEntryTime  = SILABS_GET_SLEEPTIMER_TIME();
        mEnergyTraces[mEnergyTraceCount].mEnergyMode = to;
        mEnergyTraceCount++;
    }
}

void SilabsPowerTracing::StaticPowerManagerTransitionCallback(sl_power_manager_em_t from, sl_power_manager_em_t to)
{
    Instance().PowerManagerTransitionCallback(from, to);
}

CHIP_ERROR SilabsPowerTracing::OutputPowerManagerTraces()
{
    ChipLogProgress(DeviceLayer, "=== Power Manager Energy Mode Traces ===");
    ChipLogProgress(DeviceLayer, "Index | Entry Time | Energy Mode");

    if (mEnergyTraces != nullptr && mEnergyTraceCount > 0)
    {
        for (uint32_t i = 0; i < mEnergyTraceCount; i++)
        {
            // Casting to unsigned long to remove ambiguity for the unit tests.
            ChipLogProgress(DeviceLayer, "%lu | %lu | EM%d", (unsigned long) i, (unsigned long) mEnergyTraces[i].mEntryTime,
                            mEnergyTraces[i].mEnergyMode);
            // Delay so the output is not mangled or skipped.
            // 5 (ticks) is enough for UART, but only 1 is required for RTT.
            // No delay results in missed or mangled output for both.
            osDelay(5);
        }
    }
    else
    {
        ChipLogProgress(DeviceLayer, "No energy traces recorded");
    }

    return CHIP_NO_ERROR;
}

} // namespace Silabs
} // namespace Tracing
} // namespace chip
