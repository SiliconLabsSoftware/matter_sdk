/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *    Copyright (c) 2019 Nest Labs, Inc.
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
 *          Provides an implementation of the ThreadStackManager object for
 *          EFR32 platforms using the Silicon Labs SDK and the OpenThread
 *          stack.
 *
 */
/* this file behaves like a config.h, comes first */
#include <platform/NetworkCommissioning.h>
#include <platform/OpenThread/GenericThreadStackManagerImpl_OpenThread.hpp>
#include <platform/OpenThread/OpenThreadUtils.h>
#include <platform/ThreadStackManager.h>
#include <platform/internal/CHIPDeviceLayerInternal.h>

#include <openthread/platform/entropy.h>

#if SL_USE_THREAD_DIRECT
#include <platform/silabs/address_resolve/PreCommissioning.h>
#define TD_SLW_PERIOD_SLOT 64 // 40000 us (unit of slot duration 625 us).
// Should work at 64 (40 ms)
#endif

#include <lib/support/CHIPPlatformMemory.h>

#include <openthread-core-config.h>

#include <lib/support/CodeUtils.h>
#include <mbedtls/platform.h>

extern "C" {
#include "platform-efr32.h"
otInstance * otGetInstance(void);
#if CHIP_DEVICE_CONFIG_THREAD_ENABLE_CLI
void otAppCliInit(otInstance * aInstance);
#endif // CHIP_DEVICE_CONFIG_THREAD_ENABLE_CLI
}

// for compatibility with SLCP
#ifndef SL_MATTER_OPENTHREAD_NCP_ENABLE
#define SL_MATTER_OPENTHREAD_NCP_ENABLE 0
#endif

namespace chip {
namespace DeviceLayer {
namespace {
otInstance * sOTInstance = nullptr;

}; // namespace

using namespace ::chip::DeviceLayer::Internal;

ThreadStackManagerImpl ThreadStackManagerImpl::sInstance;

CHIP_ERROR ThreadStackManagerImpl::_InitThreadStack()
{
    return InitThreadStack(sOTInstance);
}

CHIP_ERROR ThreadStackManagerImpl::_StartThreadTask()
{
    // Stubbed since our thread task is created in the InitThreadStack function and it will start once the scheduler starts.
    return CHIP_NO_ERROR;
}

void ThreadStackManagerImpl::_LockThreadStack()
{
    sl_ot_rtos_acquire_stack_mutex();
}

bool ThreadStackManagerImpl::_TryLockThreadStack()
{
    // TODO: Implement a non-blocking version of the mutex lock
    sl_ot_rtos_acquire_stack_mutex();
    return true;
}

void ThreadStackManagerImpl::_UnlockThreadStack()
{
    sl_ot_rtos_release_stack_mutex();
}

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD_SRP_CLIENT
void ThreadStackManagerImpl::_WaitOnSrpClearAllComplete()
{
    // Only 1 task can be blocked on a srpClearAll request
    if (mSrpClearAllRequester == nullptr)
    {
        mSrpClearAllRequester = osThreadGetId();
        // Wait on OnSrpClientNotification which confirms the clearing is done.
        // It will notify this current task with NotifySrpClearAllComplete.
        // However, we won't wait more than 2s.
        osThreadFlagsWait(threadSrpClearAllFlags, osFlagsWaitAny, pdMS_TO_TICKS(2000));
        mSrpClearAllRequester = nullptr;
    }
}

void ThreadStackManagerImpl::_NotifySrpClearAllComplete()
{
    if (mSrpClearAllRequester)
    {
        osThreadFlagsSet(mSrpClearAllRequester, threadSrpClearAllFlags);
    }
}
#endif // CHIP_DEVICE_CONFIG_ENABLE_THREAD_SRP_CLIENT

CHIP_ERROR ThreadStackManagerImpl::InitThreadStack(otInstance * otInst)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    err            = GenericThreadStackManagerImpl_OpenThread<ThreadStackManagerImpl>::ConfigureThreadStack(otInst);
    if (err == CHIP_NO_ERROR)
    {
        // To make sure that timeout is set with OT libraries
        otThreadSetChildTimeout(otInst, OPENTHREAD_CONFIG_MLE_CHILD_TIMEOUT_DEFAULT);
    }

    return err;
}

void ThreadStackManagerImpl::FactoryResetThreadStack()
{
    VerifyOrReturn(sOTInstance);
    otInstanceFactoryReset(sOTInstance);
}

bool ThreadStackManagerImpl::IsInitialized()
{
    return otGetInstance() != nullptr;
}

#if SL_USE_THREAD_DIRECT
CHIP_ERROR ThreadStackManagerImpl::ThreadDirectInit()
{
    VerifyOrReturnError(sOTInstance, CHIP_ERROR_INTERNAL);
// #if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
    //otExtAddress     extAddress;
    // TODO: This needs to be the target ext address, not the current one.
    // ReturnErrorOnFailure(Internal::PreCommissioning::GetInstance().GetTargetExtAddress(extAddress));
    // VerifyOrReturnError(otLinkSetExtendedAddress(sOTInstance, &extAddress) == OT_ERROR_NONE, CHIP_ERROR_INTERNAL);
// #endif
    VerifyOrReturnError(otThreadDirectSetSlwSchedule(sOTInstance, TD_SLW_PERIOD_SLOT) == OT_ERROR_NONE, CHIP_ERROR_INTERNAL);

    // TODO: Asses if necessary since this is done by ConnectivityMgr().SetThreadDeviceType
    //otLinkModeConfig config;
    // Set link mode: rx-off-when-idle sleepy end device.
    // config.mRxOnWhenIdle = false;
    // config.mDeviceType   = false;
    // config.mNetworkData  = false;
    // VerifyOrReturnError(otThreadSetLinkMode(sOTInstance, config) == OT_ERROR_NONE, CHIP_ERROR_INTERNAL);

    otThreadDirectSetEventCallback(sOTInstance, HandleDirectEvent, nullptr);

    return CHIP_NO_ERROR;
}

void ThreadStackManagerImpl::ThreadDirectSendWakeup()
{
    otError error;
    otExtAddress extAddress;
    ReturnOnFailure(Internal::PreCommissioning::GetInstance().GetTargetExtAddress(extAddress));

    ChipLogProgress(DeviceLayer,
                    "TD sending wakeup to %02X%02X%02X%02X%02X%02X%02X%02X (type=link)", extAddress.m8[0], extAddress.m8[1],
                    extAddress.m8[2], extAddress.m8[3], extAddress.m8[4], extAddress.m8[5], extAddress.m8[6], extAddress.m8[7]);

    _LockThreadStack();
    error = otThreadDirectWakeup(sOTInstance, &extAddress, OT_THREAD_DIRECT_WAKE_TYPE_LINK,
                                 0,  // interval: use configured default
                                 0,  // duration: use configured default
                                 0); // key index: use default wake key (129)
    _UnlockThreadStack();

    if (error != OT_ERROR_NONE)
    {
        ChipLogError(DeviceLayer, "TD wakeup command rejected by OpenThread: %s", otThreadErrorToString(error));
    }
}

void ThreadStackManagerImpl::HandleDirectEvent(otThreadDirectEvent aEvent, const otThreadDirectPeerInfo * aPeerInfo, void * aContext)
{
    switch (aEvent)
    {
        case OT_THREAD_DIRECT_EVENT_WAKE_RECEIVED: {
        static const char * const kWakeTypeNames[] = { "link", "power-outage", "connectionless" };
        const char * wakeTypeName                  = (aPeerInfo->mWakeType < 3) ? kWakeTypeNames[aPeerInfo->mWakeType] : "unknown";
            ChipLogProgress(DeviceLayer, "TD Wake Command received\r\n"
                        "  from:     %02X%02X%02X%02X%02X%02X%02X%02X\r\n"
                        "  type:     %s (%u)\r\n"
                        "  rv-time:  %lu us\r\n"
                        "  retries:  %u x %u intervals\r\n",
                        aPeerInfo->mExtAddress.m8[0], aPeerInfo->mExtAddress.m8[1], aPeerInfo->mExtAddress.m8[2],
                        aPeerInfo->mExtAddress.m8[3], aPeerInfo->mExtAddress.m8[4], aPeerInfo->mExtAddress.m8[5],
                        aPeerInfo->mExtAddress.m8[6], aPeerInfo->mExtAddress.m8[7], wakeTypeName, aPeerInfo->mWakeType,
                        (unsigned long) aPeerInfo->mWakeRvTimeUs, aPeerInfo->mWakeRetryCount, aPeerInfo->mWakeRetryInterval);
            if (sInstance.mThreadDirectDelegate != nullptr)
            {
                sInstance.mThreadDirectDelegate->OnThreadDirectWakeupReceived();
            }
            break;
        }

    case OT_THREAD_DIRECT_EVENT_LINKED:
        ChipLogProgress(DeviceLayer, "TD link established with %02X%02X%02X%02X%02X%02X%02X%02X\r\n", aPeerInfo->mExtAddress.m8[0],
        aPeerInfo->mExtAddress.m8[1], aPeerInfo->mExtAddress.m8[2], aPeerInfo->mExtAddress.m8[3],
        aPeerInfo->mExtAddress.m8[4], aPeerInfo->mExtAddress.m8[5], aPeerInfo->mExtAddress.m8[6],
        aPeerInfo->mExtAddress.m8[7]);
        if (sInstance.mThreadDirectDelegate != nullptr)
        {
            sInstance.mThreadDirectDelegate->OnThreadDirectLinked();
        }
        break;

    case OT_THREAD_DIRECT_EVENT_LINK_FAILED:
        if (aPeerInfo != nullptr)
        {
            ChipLogError(DeviceLayer,
                         "TD link failed with %02X%02X%02X%02X%02X%02X%02X%02X (no response / link setup failed)",
                         aPeerInfo->mExtAddress.m8[0], aPeerInfo->mExtAddress.m8[1], aPeerInfo->mExtAddress.m8[2],
                         aPeerInfo->mExtAddress.m8[3], aPeerInfo->mExtAddress.m8[4], aPeerInfo->mExtAddress.m8[5],
                         aPeerInfo->mExtAddress.m8[6], aPeerInfo->mExtAddress.m8[7]);
        }
        else
        {
            ChipLogError(DeviceLayer, "TD link failed (no peer info; wake retries exhausted or radio reject)");
        }
        if (sInstance.mThreadDirectDelegate != nullptr)
        {
            sInstance.mThreadDirectDelegate->OnThreadDirectLinkFailed();
        }
        break;

    case OT_THREAD_DIRECT_EVENT_UNLINKED:
        ChipLogProgress(DeviceLayer, "TD link unlinked\r\n");
        if (sInstance.mThreadDirectDelegate != nullptr)
        {
            sInstance.mThreadDirectDelegate->OnThreadDirectUnlinked();
        }
        break;

    default:
        break;
    }
}

#endif // SL_USE_THREAD_DIRECT

} // namespace DeviceLayer
} // namespace chip

using namespace ::chip::DeviceLayer;

#ifndef SL_COMPONENT_CATALOG_PRESENT
extern "C" __WEAK void sl_openthread_init(void)
{
#error "This shouldn't compile"
    // Place holder for enabling Silabs specific features available only through Simplicity Studio
}
#endif

/**
 * @brief Openthread UART implementation for the CLI is conflicting
 *        with the UART implemented for Pigweed RPC as they use the same UART port
 *
 *        We now only build the uart as implemented in
 *        connectedhomeip/examples/platform/efr32/uart.c
 *        and remap OT functions to use our uart api.
 *
 *        For now OT CLI isn't usable when the examples are built with pw_rpc
 */

#ifndef PW_RPC_ENABLED
#include "uart.h"
#endif

extern "C" otInstance * otGetInstance(void)
{
    return sOTInstance;
}

extern "C" void sl_ot_create_instance(void)
{
    SuccessOrDie(chip::Platform::MemoryInit());
    mbedtls_platform_set_calloc_free(CHIPPlatformMemoryCalloc, CHIPPlatformMemoryFree);
#if SL_OPENTHREAD_MULTI_PAN_ENABLE
    // Initialize multiple OT instances for Multi-PAN support
    // Instance 0: Matter protocol stack
    sOTInstance = otInstanceInitMultiple(0);
#else
    if (sOTInstance == NULL)
    {
        sOTInstance = otInstanceInitSingle();
    }
#endif // SL_OPENTHREAD_MULTI_PAN_ENABLE

    VerifyOrDie(sOTInstance != nullptr);

#if defined(OPENTHREAD_CONFIG_IP6_INIT_EXT_ADDR_POOL_ENABLE) && OPENTHREAD_CONFIG_IP6_INIT_EXT_ADDR_POOL_ENABLE
    static otNetifAddress sOtIp6UnicastPool[OPENTHREAD_CONFIG_IP6_MAX_EXT_UCAST_ADDRS];
    static otNetifMulticastAddress sOtIp6MulticastPool[OPENTHREAD_CONFIG_IP6_MAX_EXT_MCAST_ADDRS];

    // Required before otIp6SetEnabled() when cert prebuilt libs use runtime IPv6 address pools
    VerifyOrDie(otIp6Init(sOTInstance, sOtIp6UnicastPool, OPENTHREAD_CONFIG_IP6_MAX_EXT_UCAST_ADDRS, sOtIp6MulticastPool,
                          OPENTHREAD_CONFIG_IP6_MAX_EXT_MCAST_ADDRS) == OT_ERROR_NONE);
#endif
}

extern "C" void sl_ot_cli_init(void)
{
#if !defined(PW_RPC_ENABLED) && CHIP_DEVICE_CONFIG_THREAD_ENABLE_CLI
    VerifyOrDie(sOTInstance);
    otAppCliInit(sOTInstance);
#endif
}

#if CHIP_DEVICE_CONFIG_THREAD_ENABLE_CLI && !SL_MATTER_OPENTHREAD_NCP_ENABLE

extern "C" otError otPlatUartEnable(void)
{
#ifdef PW_RPC_ENABLED
    return OT_ERROR_NOT_IMPLEMENTED;
#else
    // Uart Init is handled in init_efrPlatform.cpp
    return OT_ERROR_NONE;
#endif
}

extern "C" otError otPlatUartSend(const uint8_t * aBuf, uint16_t aBufLength)
{
#ifdef PW_RPC_ENABLED
    return OT_ERROR_NOT_IMPLEMENTED;
#else
    if (uartConsoleWrite((const char *) aBuf, aBufLength) > 0)
    {
        otPlatUartSendDone();
        return OT_ERROR_NONE;
    }
    return OT_ERROR_FAILED;
#endif
}

extern "C" void efr32UartProcess(void)
{
#if !defined(PW_RPC_ENABLED) && !defined(ENABLE_CHIP_SHELL)
    uint8_t tempBuf[128] = { 0 };
    // will read the data available up to 128bytes
    int16_t count = uartConsoleRead((char *) tempBuf, 128);
    if (count > 0)
    {
        // ot process Received data for CLI cmds
        otPlatUartReceived(tempBuf, count);
    }
#endif
}

extern "C" __WEAK otError otPlatUartFlush(void)
{
    return OT_ERROR_NOT_IMPLEMENTED;
}

extern "C" __WEAK otError otPlatUartDisable(void)
{
    return OT_ERROR_NOT_IMPLEMENTED;
}

#endif // CHIP_DEVICE_CONFIG_THREAD_ENABLE_CLI && !SL_MATTER_OPENTHREAD_NCP_ENABLE
