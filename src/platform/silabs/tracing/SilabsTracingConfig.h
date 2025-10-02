/***************************************************************************
 * @file SilabsTracingConfig.h
 * @brief Configuration header for Silabs tracing functionality.
 *******************************************************************************
 * # License
 * <b>Copyright 2024 Silicon Laboratories Inc. www.silabs.com</b>
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
#pragma once

#include <matter/tracing/build_config.h>
#if defined(MATTER_TRACING_ENABLED) && MATTER_TRACING_ENABLED == 1

/**
 * @brief Centralized configuration for Silabs tracing functionality.
 */

/**
 * @brief Enable FreeRTOS runtime statistics collection for task CPU usage tracking.
 *
 * When enabled (set to 1), this enables:
 * - configGENERATE_RUN_TIME_STATS in FreeRTOS
 * - configUSE_STATS_FORMATTING_FUNCTIONS in FreeRTOS
 * - Runtime statistics timer configuration
 * - Task switching hooks for statistics collection
 *
 * Default: 0 (disabled)
 */
#ifndef TRACING_RUNTIME_STATS
#define TRACING_RUNTIME_STATS 1
#endif

/**
 * @brief Size in bytes for serialized time tracker storage.
 *
 * Default size, metrics store 6 uint32_t, which is 24 bytes
 * We currently have 19 operations to track, so 19 * 24 = 456 bytes
 * 512 bytes should be enough including the serialization overhead
 *
 * Default: 512 bytes
 */
#ifndef SERIALIZED_TIME_TRACKERS_SIZE_BYTES
#define SERIALIZED_TIME_TRACKERS_SIZE_BYTES 512
#endif

#endif // MATTER_TRACING_ENABLED
