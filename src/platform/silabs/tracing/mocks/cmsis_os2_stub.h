/***************************************************************************//**
 * @file cmsis_os2_stub.h
 * @brief Stub header for CMSIS-OS2 API (for unit tests)
 *******************************************************************************
 * # License
 * <b>Copyright 2025 Project CHIP Authors</b>
 *******************************************************************************
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ******************************************************************************/

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*osTimerFunc_t)(void * argument);
typedef void * osTimerId_t;

typedef enum {
    osTimerOnce = 0,
    osTimerPeriodic
} osTimerType_t;

typedef struct {
    const char * name;
    uint32_t attr_bits;
    void * cb_mem;
    uint32_t cb_size;
} osTimerAttr_t;

typedef enum {
    osOK = 0,
    osError = -1,
    osErrorTimeout = -2,
    osErrorResource = -3,
    osErrorParameter = -4,
    osErrorNoMemory = -5,
    osErrorISR = -6
} osStatus_t;

// Function declarations (implementations provided by test mocks)
osTimerId_t osTimerNew(osTimerFunc_t func, osTimerType_t type, void * argument, const osTimerAttr_t * attr);
uint32_t osKernelGetTickFreq(void);
osStatus_t osTimerStart(osTimerId_t timer_id, uint32_t ticks);
osStatus_t osTimerStop(osTimerId_t timer_id);
osStatus_t osTimerDelete(osTimerId_t timer_id);
osStatus_t osDelay(uint32_t ticks);

#ifdef __cplusplus
}
#endif

