

#include "taskTrace.h"
#include <rail.h>

#define MAX_TASK_ENTRY_ACCUMULATION 100
static bool doAccumulation = false;
static uint32_t count      = 0;
static slTaskInfo taskInfoArray[MAX_TASK_ENTRY_ACCUMULATION];

void startAccumulation()
{
    doAccumulation = true;
}

slTaskInfo * getTaskInfoArray()
{
    return taskInfoArray;
}

uint32_t getTaskInfoCount()
{
    return count;
}

void myTaskSwitchedInHook(uint32_t taskId)
{
    if (doAccumulation)
    {
        if (count < MAX_TASK_ENTRY_ACCUMULATION)
        {
            taskInfoArray[count].taskId    = taskId;
            taskInfoArray[count].timestamp = RAIL_GetTime() / 1000;
            count++;
        }
    }
}
