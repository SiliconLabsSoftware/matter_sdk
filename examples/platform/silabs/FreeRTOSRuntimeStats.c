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

#if defined(configGENERATE_RUN_TIME_STATS) && configGENERATE_RUN_TIME_STATS == 1

#include "FreeRTOSRuntimeStats.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

// Runtime statistics timer implementation
// These functions are called by FreeRTOS to configure and read the runtime timer

#pragma message("FreeRTOS Runtime statistics are enabled. This is a debugging feature and may have an impact on performance. Disable for release builds. Set TRACING_RUNTIME_STATS to 0 in SilabsTracingConfig.h to disable.")

// static volatile uint32_t sRunTimeCounter = 0;

// Simplified global tracking variables
static volatile uint32_t sTaskSwitchedOut = 0;
static volatile uint32_t sReadyTaskSwitchedOut = 0;
static volatile TaskHandle_t sLastTaskSwitchedOut = NULL;

// Single unified tracking array - no separate deleted tasks array
static TaskStats sTaskStats[MAX_TRACKED_TASKS];
static volatile uint32_t sTrackedTaskCount = 0;

// Helper function to find or create a task stats entry
static TaskStats* pxFindOrCreateTaskStats(TaskHandle_t handle)
{
    if (handle == NULL) return NULL;

    // Find existing entry
    for (uint32_t i = 0; i < sTrackedTaskCount; i++) {
        if (sTaskStats[i].handle == handle) {
            return &sTaskStats[i];
        }
    }

    // Create new entry if space available
    if (sTrackedTaskCount < MAX_TRACKED_TASKS) {
        TaskStats* stats = &sTaskStats[sTrackedTaskCount];
        stats->handle = handle;
        stats->switchOutCount = 0;
        stats->preemptionCount = 0;
        stats->lastSwitchOutTime = 0;
        
        // Get and store task name
        const char *taskName = pcTaskGetName(handle);
        if (taskName) {
            strncpy(stats->name, taskName, TASK_NAME_LEN - 1);
            stats->name[TASK_NAME_LEN - 1] = '\0';
        } else {
            snprintf(stats->name, TASK_NAME_LEN, "Task_%p", (void*)handle);
        }
        
        sTrackedTaskCount++;
        return stats;
    }

    return NULL;
}

void vTaskDeleted(void* xTask)
{
    // Simply mark the task as deleted in our tracking - no separate array needed
    if (xTask != NULL) {
        TaskStats* stats = pxFindOrCreateTaskStats((TaskHandle_t)xTask);
        if (stats != NULL) {
            stats->lastSwitchOutTime = ulGetRunTimeCounterValue(); // Store deletion time
            stats->handle = NULL; // Mark as deleted
        }
    }
}

uint32_t ulGetRunTimeCounterValue(void)
{
    return (xTaskGetTickCount() * 1000) / configTICK_RATE_HZ;
}

// Helper function for percentage calculation (returns percentage * 100 for 2 decimal places)
static inline uint32_t ulCalculatePercentage(uint32_t part, uint32_t total)
{
    return (total > 0) ? (part * 10000) / total : 0;
}


void vTaskSwitchedOut(){
    sTaskSwitchedOut++;
    TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
    sLastTaskSwitchedOut = currentTask;
    
    // Track per-task statistics
    TaskStats* stats = pxFindOrCreateTaskStats(currentTask);
    if (stats != NULL) {
        stats->switchOutCount++;
        stats->lastSwitchOutTime = ulGetRunTimeCounterValue();
    }
}

void vTaskSwitchedIn(){
    // Check if the last task that was switched out was in Ready state (preempted)
    if (sLastTaskSwitchedOut != NULL && eReady == eTaskGetState(sLastTaskSwitchedOut)){
        sReadyTaskSwitchedOut++;
        
        // Track per-task preemption statistics
        TaskStats* stats = pxFindOrCreateTaskStats(sLastTaskSwitchedOut);
        if (stats != NULL) {
            stats->preemptionCount++;
        }
    }
}

// New Clean API Implementation

const char* pcTaskStateToString(eTaskState state)
{
    switch (state)
    {
        case eRunning:          return "Running";
        case eReady:            return "Ready";
        case eBlocked:          return "Blocked";
        case eSuspended:        return "Suspend";
        case eDeleted:          return "Deleted";
        default:                return "Unknown";
    }
}

// Unified function to populate TaskInfo from either active task or our tracked stats
static void vPopulateTaskInfo(TaskInfo* taskInfo, const TaskStatus_t* taskStatus, const TaskStats* stats, uint32_t totalRunTime)
{
    // Initialize all fields
    memset(taskInfo, 0, sizeof(TaskInfo));
    taskInfo->isValid = true;

    if (taskStatus != NULL) {
        // Active task - populate from FreeRTOS data
        strncpy(taskInfo->name, taskStatus->pcTaskName, configMAX_TASK_NAME_LEN - 1);
        taskInfo->name[configMAX_TASK_NAME_LEN - 1] = '\0';
        taskInfo->handle = taskStatus->xHandle;
        taskInfo->priority = taskStatus->uxCurrentPriority;
        taskInfo->stackHighWaterMark = taskStatus->usStackHighWaterMark;
        taskInfo->runTimeCounter = taskStatus->ulRunTimeCounter;
        taskInfo->cpuPercentage = ulCalculatePercentage(taskStatus->ulRunTimeCounter, totalRunTime);
        
        // Copy FreeRTOS task state directly
        taskInfo->state = taskStatus->eCurrentState;
    } else if (stats != NULL && stats->handle == NULL) {
        // Deleted task - populate from our tracked stats
        strncpy(taskInfo->name, stats->name, configMAX_TASK_NAME_LEN - 1);
        taskInfo->name[configMAX_TASK_NAME_LEN - 1] = '\0';
        taskInfo->handle = NULL;
        taskInfo->state = eDeleted;  // Use standard FreeRTOS deleted state
        taskInfo->lastExecutionTime = stats->lastSwitchOutTime;
        // Other fields remain 0 as initialized
    }

    // Add preemption statistics if we have tracking data
    if (stats != NULL) {
        taskInfo->switchOutCount = stats->switchOutCount;
        taskInfo->preemptionCount = stats->preemptionCount;
        taskInfo->preemptionPercentage = ulCalculatePercentage(stats->preemptionCount, stats->switchOutCount);
        if (taskStatus != NULL) {
            taskInfo->lastExecutionTime = stats->lastSwitchOutTime;
        }
    }
}

uint32_t ulGetAllTaskInfo(TaskInfo* taskInfoArray, uint32_t maxTasks, SystemTaskStats* systemStats)
{
    if (taskInfoArray == NULL) return 0;
    
    uint32_t taskCount = 0;
    uint32_t deletedTaskCount = 0;
    
    // Initialize system stats
    if (systemStats != NULL) {
        memset(systemStats, 0, sizeof(SystemTaskStats));
        systemStats->totalRunTime = ulGetRunTimeCounterValue();
        systemStats->totalSwitchOutCount = sTaskSwitchedOut;
        systemStats->totalPreemptionCount = sReadyTaskSwitchedOut;
        systemStats->systemPreemptionRatio = ulCalculatePercentage(sReadyTaskSwitchedOut, sTaskSwitchedOut);
    }
    
    // Get active tasks from FreeRTOS
    UBaseType_t uxArraySize = uxTaskGetNumberOfTasks();
    TaskStatus_t * pxTaskStatusArray = (TaskStatus_t *) pvPortMalloc(uxArraySize * sizeof(TaskStatus_t));
    
    if (pxTaskStatusArray != NULL) {
        uint32_t ulTotalRunTime;
        uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, &ulTotalRunTime);
        
        // Process active tasks
        for (UBaseType_t i = 0; i < uxArraySize && taskCount < maxTasks; i++) {
            TaskStats* stats = pxFindOrCreateTaskStats(pxTaskStatusArray[i].xHandle);
            vPopulateTaskInfo(&taskInfoArray[taskCount], &pxTaskStatusArray[i], stats, ulTotalRunTime);
            taskCount++;
        }
        
        // Update system stats
        if (systemStats != NULL) {
            systemStats->activeTaskCount = uxArraySize;
            if (systemStats->totalRunTime < ulTotalRunTime) {
                systemStats->totalRunTime = ulTotalRunTime;
            }
        }
        
        vPortFree(pxTaskStatusArray);
    }
    
    // Add terminated tasks from our tracking
    for (uint32_t i = 0; i < sTrackedTaskCount && taskCount < maxTasks; i++) {
        if (sTaskStats[i].handle == NULL) { // Deleted task
            vPopulateTaskInfo(&taskInfoArray[taskCount], NULL, &sTaskStats[i], 0);
            taskCount++;
            deletedTaskCount++;
        }
    }
    
    // Final system stats update
    if (systemStats != NULL) {
        systemStats->terminatedTaskCount = deletedTaskCount;
        systemStats->totalTaskCount = taskCount;
    }
    
    return taskCount;
}

#endif // configGENERATE_RUN_TIME_STATS == 1