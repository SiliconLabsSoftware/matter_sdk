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

#if 1

#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>
#include <stdbool.h>


// Use standard FreeRTOS task states (eRunning, eReady, eBlocked, eSuspended, eDeleted)
// No need for custom enum since FreeRTOS already provides what we need

#define MAX_TRACKED_TASKS 32
#define TASK_NAME_LEN     configMAX_TASK_NAME_LEN

// Unified task information structure
typedef struct {
    char name[configMAX_TASK_NAME_LEN];
    TaskHandle_t handle;                    // NULL for deleted tasks we're tracking historically
    eTaskState state;                       // Standard FreeRTOS task state
    UBaseType_t priority;                   // 0 for deleted tasks
    UBaseType_t stackHighWaterMark;         // 0 for deleted tasks
    uint32_t runTimeCounter;                // Total CPU time
    uint32_t cpuPercentage;                 // CPU usage percentage (x100 for 2 decimal places)
    uint32_t switchOutCount;                // Total times switched out
    uint32_t preemptionCount;               // Times preempted (switched out while ready)
    uint32_t preemptionPercentage;          // Preemption percentage (x100 for 2 decimal places)
    uint32_t lastExecutionTime;             // Last time this task ran (for deleted) or switch time
    bool isValid;                           // true if this entry contains valid data
} TaskInfo;

// System-wide task statistics
typedef struct {
    uint32_t totalRunTime;                  // Total system run time in ms
    uint32_t totalSwitchOutCount;           // Total task switches
    uint32_t totalPreemptionCount;          // Total preemptions
    uint32_t systemPreemptionRatio;         // Overall preemption ratio (x100)
    uint32_t activeTaskCount;               // Number of currently active tasks
    uint32_t terminatedTaskCount;           // Number of deleted tasks we're tracking historically
    uint32_t totalTaskCount;                // activeTaskCount + terminatedTaskCount
} SystemTaskStats;

typedef struct {
    TaskHandle_t handle;
    char name[TASK_NAME_LEN];
    uint32_t switchOutCount;
    uint32_t preemptionCount;
    uint32_t lastSwitchOutTime;
} TaskStats;



// API Functions

/**
 * @brief Get comprehensive task statistics including active and deleted tasks
 * @param taskInfoArray Array to store task information
 * @param maxTasks Maximum number of tasks the array can hold
 * @param systemStats Pointer to store system-wide statistics
 * @return Number of tasks actually returned, or 0 on error
 */
uint32_t ulGetAllTaskInfo(TaskInfo* taskInfoArray, uint32_t maxTasks, SystemTaskStats* systemStats);

// Helper functions for state conversion
const char* pcTaskStateToString(eTaskState state);




#endif // configGENERATE_RUN_TIME_STATS == 1