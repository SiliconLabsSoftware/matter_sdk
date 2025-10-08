/***************************************************************************
 * @file FreeRTOSRuntimeStats.h
 * @brief FreeRTOS runtime statistics API for Silicon Labs platform.
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

// #include "FreeRTOSConfig.h"

#if defined(configGENERATE_RUN_TIME_STATS) && configGENERATE_RUN_TIME_STATS == 1

#include "FreeRTOS.h"
#include "task.h"
#include <platform/silabs/tracing/SilabsTracingConfig.h>
#include <stdbool.h>
#include <stdint.h>
typedef struct
{
    char name[configMAX_TASK_NAME_LEN];
    TaskHandle_t handle;
    eTaskState state;
    UBaseType_t priority;
    UBaseType_t stackHighWaterMark;
    uint32_t runTimeCounter;       // Total CPU time
    uint32_t cpuPercentage;        // CPU usage percentage
    uint32_t switchOutCount;       // Total times switched out
    uint32_t preemptionCount;      // Times preempted (switched out while ready)
    uint32_t preemptionPercentage; // Preemption percentage
    uint32_t lastExecutionTime;    // Last execution time
} TaskInfo;

// System-wide task statistics
typedef struct
{
    uint32_t totalRunTime;          // Total system run time in ms
    uint32_t totalSwitchOutCount;   // Total task switches
    uint32_t totalPreemptionCount;  // Total preemptions
    uint32_t systemPreemptionRatio; // Overall preemption ratio
    uint32_t activeTaskCount;       // Number of currently active tasks
    uint32_t terminatedTaskCount;   // Number of deleted tasks we're tracking
    uint32_t totalTaskCount;        // activeTaskCount + terminatedTaskCount
} SystemTaskStats;

// Internal task tracking structure
typedef struct
{
    TaskHandle_t handle; // NULL for deleted tasks
    char name[configMAX_TASK_NAME_LEN];
    uint32_t switchOutCount;
    uint32_t preemptionCount;
    uint32_t lastSwitchOutTime;
    bool isDeleted; // true if this task has been deleted
} TaskStats;

/**
 * @brief Get comprehensive task statistics for active and deleted tasks
 * @param taskInfoArray Array to store task information
 * @param taskInfoArraySize Maximum number of tasks the array can hold
 * @param systemStats Pointer to store system-wide statistics (optional)
 * @return Number of tasks actually returned, or 0 on error
 */
uint32_t ulGetAllTaskInfo(TaskInfo * taskInfoArray, uint32_t taskInfoArraySize, SystemTaskStats * systemStats);

#endif // configGENERATE_RUN_TIME_STATS == 1
