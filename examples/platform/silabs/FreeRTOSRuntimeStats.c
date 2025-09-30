/***************************************************************************
 * @file FreeRTOSRuntimeStats.c
 * @brief FreeRTOS runtime statistics timer implementation for Silicon Labs platform.
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
#include "FreeRTOSConfig.h"

#if configGENERATE_RUN_TIME_STATS == 1

#include "FreeRTOS.h"
#include "task.h"

// Runtime statistics timer implementation
// These functions are called by FreeRTOS to configure and read the runtime timer

static volatile uint32_t sRunTimeCounter = 0;

void vConfigureTimerForRunTimeStats(void)
{
    // Initialize the runtime counter.
    // A simple counter incremented by task switches
    sRunTimeCounter = 0;
}

uint32_t ulGetRunTimeCounterValue(void)
{
    return sRunTimeCounter;
}

void vIncrementRunTimeCounter(void)
{
    // Called from task switch hook
    sRunTimeCounter++;
}

#endif // configGENERATE_RUN_TIME_STATS == 1
