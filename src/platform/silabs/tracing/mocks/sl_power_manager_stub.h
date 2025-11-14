/***************************************************************************/ /**
                                                                               * @file sl_power_manager_stub.h
                                                                               * @brief Stub header for Silicon Labs Power Manager
                                                                               *API (for unit tests)
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

#pragma once

#include <cstddef>
#include <stdint.h>

// Default values for tracing config constants if not defined by build system
#ifndef SILABS_TRACING_ENERGY_TRACES_MAX
#define SILABS_TRACING_ENERGY_TRACES_MAX 100
#endif
#ifndef SILABS_TRACING_ENERGY_TRACES_SECONDS
#define SILABS_TRACING_ENERGY_TRACES_SECONDS 10
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Power transition events
#define SL_POWER_MANAGER_EVENT_TRANSITION_ENTERING_EM0 (1 << 0)
#define SL_POWER_MANAGER_EVENT_TRANSITION_LEAVING_EM0 (1 << 1)
#define SL_POWER_MANAGER_EVENT_TRANSITION_ENTERING_EM1 (1 << 2)
#define SL_POWER_MANAGER_EVENT_TRANSITION_LEAVING_EM1 (1 << 3)
#define SL_POWER_MANAGER_EVENT_TRANSITION_ENTERING_EM2 (1 << 4)
#define SL_POWER_MANAGER_EVENT_TRANSITION_LEAVING_EM2 (1 << 5)

// Energy modes
typedef enum
{
    SL_POWER_MANAGER_EM0 = 0, ///< Run Mode (Energy Mode 0)
    SL_POWER_MANAGER_EM1,     ///< Sleep Mode (Energy Mode 1)
    SL_POWER_MANAGER_EM2,     ///< Deep Sleep Mode (Energy Mode 2)
    SL_POWER_MANAGER_EM3,     ///< Stop Mode (Energy Mode 3)
    SL_POWER_MANAGER_EM4,     ///< Shutoff Mode (Energy Mode 4)
} sl_power_manager_em_t;

typedef uint32_t sl_power_manager_em_transition_event_t;

typedef void (*sl_power_manager_em_transition_on_event_t)(sl_power_manager_em_t from, sl_power_manager_em_t to);

typedef struct
{
    const sl_power_manager_em_transition_event_t event_mask;
    const sl_power_manager_em_transition_on_event_t on_event;
} sl_power_manager_em_transition_event_info_t;

typedef struct
{
    void * node;
    const sl_power_manager_em_transition_event_info_t * info;
} sl_power_manager_em_transition_event_handle_t;

// Function declarations (implementations provided by test mocks)
void sl_power_manager_init(void);
void sl_power_manager_subscribe_em_transition_event(void * handle, void * info);
void sl_power_manager_unsubscribe_em_transition_event(void * handle);

#ifdef __cplusplus
}
#endif
