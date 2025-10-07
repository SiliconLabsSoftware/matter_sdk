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

static volatile uint32_t sDeletedTasksCount = 0;

static volatile uint32_t sTaskSwitchedOut = 0;

static volatile uint32_t sReadyTaskSwitchedOut = 0;

static volatile TaskHandle_t sLastTaskSwitchedOut = NULL;



typedef struct {
    char name[TASK_NAME_LEN];
    uint32_t deletedTime;
} DeletedTaskInfo;



static DeletedTaskInfo sDeletedTasks[MAX_DELETED_TASKS];
static TaskStats sTaskStats[MAX_TRACKED_TASKS];
static volatile uint32_t sTrackedTaskCount = 0;

// Helper function to find or create a task stats entry
static TaskStats* pxFindOrCreateTaskStats(TaskHandle_t handle)
{
    if (handle == NULL)
        return NULL;

    // First, try to find existing entry
    for (uint32_t i = 0; i < sTrackedTaskCount; i++) {
        if (sTaskStats[i].handle == handle) {
            return &sTaskStats[i];
        }
    }

    // If not found and we have space, create new entry
    if (sTrackedTaskCount < MAX_TRACKED_TASKS) {
        TaskStats* stats = &sTaskStats[sTrackedTaskCount];
        stats->handle = handle;
        stats->switchOutCount = 0;
        stats->preemptionCount = 0;
        stats->lastSwitchOutTime = 0;
        
        // Get task name
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

    return NULL; // No space available
}

void vTaskDeleted(void* xTask)
{
    // Called from task delete hook
    if (sDeletedTasksCount < MAX_DELETED_TASKS && xTask != NULL) {
        TaskHandle_t handle = (TaskHandle_t)xTask;
        const char *taskName = pcTaskGetName(handle);
        if (taskName) {
            strncpy(sDeletedTasks[sDeletedTasksCount].name, taskName, TASK_NAME_LEN - 1);
            sDeletedTasks[sDeletedTasksCount].name[TASK_NAME_LEN - 1] = '\0';
        } else {
            sDeletedTasks[sDeletedTasksCount].name[0] = '\0';
        }
        sDeletedTasks[sDeletedTasksCount].deletedTime = ulGetRunTimeCounterValue();
        sDeletedTasksCount++;
    }
}

uint32_t ulGetRunTimeCounterValue(void)
{
    return (xTaskGetTickCount() * 1000) / configTICK_RATE_HZ;
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

const char* pcTaskStateToString(eTaskStateExtended state)
{
    switch (state)
    {
        case eTaskStateRunning:     return "Running";
        case eTaskStateReady:       return "Ready";
        case eTaskStateBlocked:     return "Blocked";
        case eTaskStateSuspended:   return "Suspend";
        case eTaskStateDeleted:     return "Deleted";
        case eTaskStateTerminated:  return "Terminated";
        default:                    return "Unknown";
    }
}

// Helper function to populate TaskInfo from active task
static void vPopulateTaskInfoFromActive(TaskInfo* taskInfo, const TaskStatus_t* taskStatus, uint32_t totalRunTime)
{
    // Copy task name
    strncpy(taskInfo->name, taskStatus->pcTaskName, configMAX_TASK_NAME_LEN - 1);
    taskInfo->name[configMAX_TASK_NAME_LEN - 1] = '\0';
    
    taskInfo->handle = taskStatus->xHandle;
    
    // Convert FreeRTOS state to extended state
    switch (taskStatus->eCurrentState)
    {
        case eRunning:      taskInfo->state = eTaskStateRunning; break;
        case eReady:        taskInfo->state = eTaskStateReady; break;
        case eBlocked:      taskInfo->state = eTaskStateBlocked; break;
        case eSuspended:    taskInfo->state = eTaskStateSuspended; break;
        case eDeleted:      taskInfo->state = eTaskStateDeleted; break;
        default:            taskInfo->state = eTaskStateDeleted; break;
    }
    
    taskInfo->priority = taskStatus->uxCurrentPriority;
    taskInfo->stackHighWaterMark = taskStatus->usStackHighWaterMark;
    taskInfo->runTimeCounter = taskStatus->ulRunTimeCounter;
    
    // Calculate CPU percentage with 2 decimal places (x100)
    if (totalRunTime > 0)
    {
        taskInfo->cpuPercentage = (taskStatus->ulRunTimeCounter * 10000) / totalRunTime;
    }
    else
    {
        taskInfo->cpuPercentage = 0;
    }
    
    // Get preemption statistics
    TaskStats* stats = pxFindOrCreateTaskStats(taskStatus->xHandle);
    if (stats != NULL)
    {
        taskInfo->switchOutCount = stats->switchOutCount;
        taskInfo->preemptionCount = stats->preemptionCount;
        taskInfo->lastExecutionTime = stats->lastSwitchOutTime;
        
        // Calculate preemption percentage with 2 decimal places (x100)
        if (stats->switchOutCount > 0)
        {
            taskInfo->preemptionPercentage = (stats->preemptionCount * 10000) / stats->switchOutCount;
        }
        else
        {
            taskInfo->preemptionPercentage = 0;
        }
    }
    else
    {
        taskInfo->switchOutCount = 0;
        taskInfo->preemptionCount = 0;
        taskInfo->preemptionPercentage = 0;
        taskInfo->lastExecutionTime = 0;
    }
    
    taskInfo->isValid = true;
}

// Helper function to populate TaskInfo from terminated task
static void vPopulateTaskInfoFromTerminated(TaskInfo* taskInfo, const DeletedTaskInfo* deletedTask)
{
    strncpy(taskInfo->name, deletedTask->name, configMAX_TASK_NAME_LEN - 1);
    taskInfo->name[configMAX_TASK_NAME_LEN - 1] = '\0';
    
    taskInfo->handle = NULL;
    taskInfo->state = eTaskStateTerminated;
    taskInfo->priority = 0;
    taskInfo->stackHighWaterMark = 0;
    taskInfo->runTimeCounter = 0;
    taskInfo->cpuPercentage = 0;
    taskInfo->switchOutCount = 0;
    taskInfo->preemptionCount = 0;
    taskInfo->preemptionPercentage = 0;
    taskInfo->lastExecutionTime = deletedTask->deletedTime;
    taskInfo->isValid = true;
}

uint32_t ulGetAllTaskInfo(TaskInfo* taskInfoArray, uint32_t maxTasks, SystemTaskStats* systemStats)
{
    if (taskInfoArray == NULL)
        return 0;
    
    uint32_t taskCount = 0;
    
    // Initialize system stats if provided
    if (systemStats != NULL)
    {
        memset(systemStats, 0, sizeof(SystemTaskStats));
        systemStats->totalRunTime = ulGetRunTimeCounterValue();
        systemStats->totalSwitchOutCount = sTaskSwitchedOut;
        systemStats->totalPreemptionCount = sReadyTaskSwitchedOut;
        if (sTaskSwitchedOut > 0)
        {
            systemStats->systemPreemptionRatio = (sReadyTaskSwitchedOut * 10000) / sTaskSwitchedOut;
        }
        systemStats->terminatedTaskCount = sDeletedTasksCount;
    }
    
    // Get active tasks
    UBaseType_t uxArraySize = uxTaskGetNumberOfTasks();
    TaskStatus_t * pxTaskStatusArray = (TaskStatus_t *) pvPortMalloc(uxArraySize * sizeof(TaskStatus_t));
    
    if (pxTaskStatusArray != NULL)
    {
        uint32_t ulTotalRunTime;
        uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, &ulTotalRunTime);
        
        // Add active tasks
        for (UBaseType_t i = 0; i < uxArraySize && taskCount < maxTasks; i++)
        {
            vPopulateTaskInfoFromActive(&taskInfoArray[taskCount], &pxTaskStatusArray[i], ulTotalRunTime);
            taskCount++;
        }
        
        if (systemStats != NULL)
        {
            systemStats->activeTaskCount = uxArraySize;
            if (systemStats->totalRunTime == 0)
                systemStats->totalRunTime = ulTotalRunTime;
        }
        
        vPortFree(pxTaskStatusArray);
    }
    
    // Add terminated tasks
    for (uint32_t i = 0; i < sDeletedTasksCount && taskCount < maxTasks; i++)
    {
        vPopulateTaskInfoFromTerminated(&taskInfoArray[taskCount], &sDeletedTasks[i]);
        taskCount++;
    }
    
    if (systemStats != NULL)
    {
        systemStats->totalTaskCount = taskCount;
    }
    
    return taskCount;
}

#endif // configGENERATE_RUN_TIME_STATS == 1