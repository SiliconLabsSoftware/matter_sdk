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
    UBaseType_t stackMaxSize;
    uint32_t runTimeCounter; // Total CPU time in ms
    uint32_t cpuPercentage;
    uint32_t switchOutCount;  // Total times switched out
    uint32_t preemptionCount; // Times preempted (switched out while ready)
    uint32_t preemptionPercentage;
    uint32_t lastExecutionTime;      // in ms
    uint32_t readyTimeHighWaterMark; // in ms
    uint32_t totalReadyTime;         // in ms
    uint32_t totalRunningTime;
} TaskInfo;
typedef struct
{
    uint32_t totalRunTime; // Total system run time in ms
    uint32_t totalSwitchOutCount;
    uint32_t totalPreemptionCount;
    uint32_t systemPreemptionRatio;
    uint32_t activeTaskCount;
    uint32_t terminatedTaskCount;
    uint32_t totalTaskCount;
} SystemTaskStats;

typedef struct
{
    TaskHandle_t handle;
    char name[configMAX_TASK_NAME_LEN];
    uint32_t switchOutCount;
    uint32_t preemptionCount;
    uint32_t lastSwitchOutTime;
    uint32_t lastMovedToReadyTime;
    uint32_t totalReadyTime;
    uint32_t readyTimeHighWaterMark;
    uint32_t lastMovedToRunningTime;
    uint32_t totalRunningTime;
    bool isDeleted;
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
