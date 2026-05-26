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
 *          Provides an implementation of the ThreadStackManager object
 *          for EFR32 platforms using the Silicon Labs SDK and the OpenThread
 *          stack.
 */

#pragma once

#include <platform/OpenThread/GenericThreadStackManagerImpl_OpenThread.h>

#include <openthread/tasklet.h>
#include <openthread/thread.h>

#include "cmsis_os2.h"

#if SL_USE_THREAD_DIRECT
#include <openthread/thread_direct.h>
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD_SRP_CLIENT
static constexpr uint32_t threadSrpClearAllFlags = 0x0001U;
#endif

extern "C" void otSysEventSignalPending(void);

namespace chip {
namespace DeviceLayer {

class ThreadStackManager;
class ThreadStackManagerImpl;
namespace Internal {
extern int GetEntropy_EFR32(uint8_t * buf, size_t bufSize);
}


#if SL_USE_THREAD_DIRECT
/**
 * Notifies application code of the outcome of a Thread Direct wakeup, without
 * exposing OpenThread types (otThreadDirectEvent, otThreadDirectPeerInfo) outside
 * the platform layer.
 */
class ThreadDirectDelegate
{
public:
    virtual ~ThreadDirectDelegate() = default;

    /// Called when a wakeup command is received from the target.
    virtual void OnThreadDirectWakeupReceived() {}
    /// Called when a link with the target of a prior ThreadDirectSendWakeup() is established.
    virtual void OnThreadDirectLinked() {}
    /// Called when a wakeup attempt fails to establish a link (e.g. no response from target).
    virtual void OnThreadDirectLinkFailed() {}
    /// Called when a link with the target is terminated.
    virtual void OnThreadDirectUnlinked() {}
};
#endif // SL_USE_THREAD_DIRECT

/**
 * Concrete implementation of the ThreadStackManager singleton object for EFR32 platforms
 * using the Silicon Labs SDK and the OpenThread stack.
 */
class ThreadStackManagerImpl final : public ThreadStackManager,
                                     public Internal::GenericThreadStackManagerImpl_OpenThread<ThreadStackManagerImpl>
{
    // Allow the ThreadStackManager interface class to delegate method calls to
    // the implementation methods provided by this class.
    friend class ThreadStackManager;

    // Allow the generic implementation base classes to call helper methods on
    // this class.
#ifndef DOXYGEN_SHOULD_SKIP_THIS
    friend Internal::GenericThreadStackManagerImpl_OpenThread<ThreadStackManagerImpl>;
#endif

    // Allow glue functions called by OpenThread to call helper methods on this
    // class.
    friend void ::otTaskletsSignalPending(otInstance * otInst);
    friend void ::otSysEventSignalPending();

public:
    // ===== Platform-specific members that may be accessed directly by the application.

    using ThreadStackManager::InitThreadStack;
    CHIP_ERROR InitThreadStack(otInstance * otInst);
    void FactoryResetThreadStack();

#if SL_USE_THREAD_DIRECT
    CHIP_ERROR ThreadDirectInit();
    void ThreadDirectSendWakeup();
    void SetThreadDirectDelegate(ThreadDirectDelegate * delegate) { mThreadDirectDelegate = delegate; }

    // TODO: Confirm needed before merging
    ThreadDirectDelegate * GetThreadDirectDelegate() const { return mThreadDirectDelegate; }
#endif // SL_USE_THREAD_DIRECT

private:
    // ===== Methods that implement the ThreadStackManager abstract interface.

    CHIP_ERROR _InitThreadStack();
    CHIP_ERROR _StartThreadTask();
    void _LockThreadStack();
    bool _TryLockThreadStack();
    void _UnlockThreadStack();

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD_SRP_CLIENT
    void _WaitOnSrpClearAllComplete();
    void _NotifySrpClearAllComplete();
#endif // CHIP_DEVICE_CONFIG_ENABLE_THREAD_SRP_CLIENT
    // ===== Members for internal use by the following friends.

    friend ThreadStackManager & ::chip::DeviceLayer::ThreadStackMgr();
    friend ThreadStackManagerImpl & ::chip::DeviceLayer::ThreadStackMgrImpl();
    friend int Internal::GetEntropy_EFR32(uint8_t * buf, size_t bufSize);

    static ThreadStackManagerImpl sInstance;

    static bool IsInitialized();

    // ===== Private members for use by this class only.

    ThreadStackManagerImpl() = default;

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD_SRP_CLIENT
    osThreadId_t mSrpClearAllRequester = NULL;
#endif

#if SL_USE_THREAD_DIRECT
    static void HandleDirectEvent(otThreadDirectEvent aEvent, const otThreadDirectPeerInfo * aPeerInfo, void * aContext);

    ThreadDirectDelegate * mThreadDirectDelegate = nullptr;
#endif // SL_USE_THREAD_DIRECT
};

/**
 * Returns the public interface of the ThreadStackManager singleton object.
 *
 * Chip applications should use this to access features of the ThreadStackManager object
 * that are common to all platforms.
 */
inline ThreadStackManager & ThreadStackMgr()
{
    return ThreadStackManagerImpl::sInstance;
}

/**
 * Returns the platform-specific implementation of the ThreadStackManager singleton object.
 *
 * Chip applications can use this to gain access to features of the ThreadStackManager
 * that are specific to EFR32 platforms.
 */
inline ThreadStackManagerImpl & ThreadStackMgrImpl()
{
    return ThreadStackManagerImpl::sInstance;
}

} // namespace DeviceLayer
} // namespace chip
