/***************************************************************************
 * @file PowerManagerTracing.cpp
 * @brief Tracing operations relating to the Silabs Power Manager
 *******************************************************************************
 * # License
 * <b>Copyright 2025 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/

#include <platform/silabs/tracing/SilabsTracingMacros.h>
#include <sl_power_manager.h>

#define EM_EVENT_MASK_ALL (SL_POWER_MANAGER_EVENT_TRANSITION_ENTERING_EM2 | SL_POWER_MANAGER_EVENT_TRANSITION_LEAVING_EM2)

namespace chip {
namespace Tracing {
namespace Silabs {

namespace {

/**
 * @brief Convert power manager state to time trace operation.
 *
 * @param state Power manager state.
 *
 * @return TimeTraceOperation Corresponding time trace operation.
 *          If the state is not recognized, returns kNumTraces.
 */
TimeTraceOperation ConvertPMToTimeTraceOperation(sl_power_manager_em_t state)
{
    switch (state)
    {
    case SL_POWER_MANAGER_EM0:
        return TimeTraceOperation::kEM0PowerMode;
    case SL_POWER_MANAGER_EM1:
        return TimeTraceOperation::kEM1PowerMode;
    case SL_POWER_MANAGER_EM2:
        return TimeTraceOperation::kEM2PowerMode;
    case SL_POWER_MANAGER_EM3:
        return TimeTraceOperation::kEM3PowerMode;
    case SL_POWER_MANAGER_EM4:
        return TimeTraceOperation::kEM4PowerMode;
    default:
        return TimeTraceOperation::kNumTraces;
    }
}

/**
 * @brief Callback function for power manager state transitions.
 *        This function is called whenever the power manager transitions
 *        between different energy modes. It then logs the transition.
 *
 * @param from Previous power manager state.
 * @param to   New power manager state.
 *
 */
void PowerManagerTransitionCallback(sl_power_manager_em_t from, sl_power_manager_em_t to)
{
    SILABS_TRACE_END(ConvertPMToTimeTraceOperation(from));
    SILABS_TRACE_BEGIN(ConvertPMToTimeTraceOperation(to));
}

sl_power_manager_em_transition_event_handle_t handle;
sl_power_manager_em_transition_event_info_t info = { .event_mask = EM_EVENT_MASK_ALL, .on_event = PowerManagerTransitionCallback };

} // namespace

void RegisterPowerManagerTracing()
{
    sl_power_manager_subscribe_em_transition_event(&handle, &info);
}

} // namespace Silabs
} // namespace Tracing
} // namespace chip
