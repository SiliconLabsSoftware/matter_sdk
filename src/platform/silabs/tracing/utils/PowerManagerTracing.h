/***************************************************************************
 * @file PowerManagerTracing.h
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

namespace chip {

namespace Tracing {
namespace Silabs {

/**
 * @brief Registers the power manager tracing functionnality with the power manager to receive notifications when the devices
 *        changes its power state.
 */
void RegisterPowerManagerTracing();

} // namespace Silabs
} // namespace Tracing
} // namespace chip
