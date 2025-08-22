#pragma once
#include <stdint.h>

typedef struct
{
    uint32_t taskId;
    uint32_t timestamp;

} slTaskInfo;

void startAccumulation();
slTaskInfo * getTaskInfoArray();
uint32_t getTaskInfoCount();

void myTaskSwitchedInHook(uint32_t taskId);
