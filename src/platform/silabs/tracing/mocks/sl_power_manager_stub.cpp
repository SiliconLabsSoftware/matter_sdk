/***************************************************************************//**
 * @file sl_power_manager_stub.cpp
 * @brief Stub implementation for Silicon Labs Power Manager API (for unit tests)
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

#include "sl_power_manager_stub.h"

// Minimal stub implementations for unit tests
// These functions do nothing but simulate successful execution

void sl_power_manager_init(void)
{
    // No-op for unit tests
}

void sl_power_manager_subscribe_em_transition_event(void * handle, void * info)
{
    // No-op for unit tests
}

void sl_power_manager_unsubscribe_em_transition_event(void * handle)
{
    // No-op for unit tests
}
