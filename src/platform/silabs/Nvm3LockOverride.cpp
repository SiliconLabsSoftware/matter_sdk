/*
 *
 *    Copyright (c) 2026 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 *    @file
 *          Override the nvm3 lock functions to use a mutex with recursive and priority inheritance.
 *          Currently this is only used for Series 2.
 *          For series 3 and SiWx917, the default nvm3 lock, from sisdk, is already using a mutex.
 */

#include <cmsis_os2.h>
#include <lib/support/CodeUtils.h>
#include <nvm3_lock.h>

namespace {
// Substitute the GSDK weak nvm3_lockBegin and nvm3_lockEnd
// for an application controlled re-entrance protection
osMutexId_t gNvm3Mutex             = nullptr;
const osMutexAttr_t kNvm3MutexAttr = { "nvm3_mutex", osMutexRecursive | osMutexPrioInherit, NULL, 0 };
} // namespace

// For EFR32MG24, use a binary semaphore to protect the nvm3 access
// For other series, use the default nvm3_lockBegin and nvm3_lockEnd from nvm3_lock.c
void nvm3_lockBegin(void)
{
    // if kernel is not running yet, we don't need to lock.
    if (osKernelGetState() == osKernelRunning)
    {
        // Initialize the mutex if it's not already done.
        if (gNvm3Mutex == nullptr)
        {
            gNvm3Mutex = osMutexNew(&kNvm3MutexAttr);
        }

        osStatus_t status = osMutexAcquire(gNvm3Mutex, osWaitForever);
        // Avoid doing nvm3 actions during ISR, but if happens, it's safe to continue without the lock.
        // Any other error status should not happen.
        VerifyOrDie(status == osOK || status == osErrorISR);
    }
}

void nvm3_lockEnd(void)
{
    // if kernel is not running yet, we don't need to lock.
    if (osKernelGetState() == osKernelRunning)
    {
        osStatus_t status = osMutexRelease(gNvm3Mutex);
        VerifyOrDie(status == osOK || status == osErrorISR);
    }
}
