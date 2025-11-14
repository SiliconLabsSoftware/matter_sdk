/***************************************************************************/ /**
                                                                               * @file cmsis_os2_stub.cpp
                                                                               * @brief Stub implementation for CMSIS-OS2 API (for
                                                                               *unit tests)
                                                                               *******************************************************************************
                                                                               * # License
                                                                               * <b>Copyright 2025 Project CHIP Authors</b>
                                                                               *******************************************************************************
                                                                               *
                                                                               * Licensed under the Apache License, Version 2.0 (the
                                                                               *"License"); you may not use this file except in
                                                                               *compliance with the License. You may obtain a copy
                                                                               *of the License at
                                                                               *
                                                                               *     http://www.apache.org/licenses/LICENSE-2.0
                                                                               *
                                                                               * Unless required by applicable law or agreed to in
                                                                               *writing, software distributed under the License is
                                                                               *distributed on an "AS IS" BASIS, WITHOUT WARRANTIES
                                                                               *OR CONDITIONS OF ANY KIND, either express or
                                                                               *implied. See the License for the specific language
                                                                               *governing permissions and limitations under the
                                                                               *License.
                                                                               ******************************************************************************/

#include "cmsis_os2_stub.h"

// Minimal stub implementations for unit tests
// These functions do nothing but return success

osTimerId_t osTimerNew(osTimerFunc_t func, osTimerType_t type, void * argument, const osTimerAttr_t * attr)
{
    // Return a non-null pointer to simulate successful timer creation
    static int dummy_timer = 1;
    return &dummy_timer;
}

uint32_t osKernelGetTickFreq(void)
{
    // Return 1000 Hz (1 tick = 1 ms)
    return 1000;
}

osStatus_t osTimerStart(osTimerId_t timer_id, uint32_t ticks)
{
    return osOK;
}

osStatus_t osTimerStop(osTimerId_t timer_id)
{
    return osOK;
}

osStatus_t osTimerDelete(osTimerId_t timer_id)
{
    return osOK;
}

osStatus_t osDelay(uint32_t ticks)
{
    return osOK;
}
