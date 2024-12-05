/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *    Copyright (c) 2018 Nest Labs, Inc.
 *    All rights reserved.
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

#pragma once

#include <app/util/basic-types.h>
#include <lib/core/CHIPError.h>
#include <lib/support/CodeUtils.h>

#include "AppEvent.h"
#include <cmsis_os2.h>
#include <string>

class LightSwitchMgr
{
public:
    enum class LightSwitchAction : uint8_t
    {
        Toggle, // Triggers an toggle command
        On,     // Triggers an On command
        Off     // Trigger an Off command
    };

    struct GenericSwitchEventData
    {
        chip::EndpointId endpoint;
        chip::EventId event;
    };
    struct Timer
    {
        typedef void (*Callback)(Timer & timer);

        Timer(uint32_t timeoutInMs, Callback callback, void * context);
        ~Timer();

        void Start();
        void Stop();
        void Timeout();

        Callback mCallback = nullptr;
        void * mContext    = nullptr;
        bool mIsActive     = false;

        osTimerId_t mHandler = nullptr;

    private:
        static void TimerCallback(void * timerCbArg);
    };
    CHIP_ERROR Init(chip::EndpointId lightSwitchEndpoint, chip::EndpointId genericSwitchEndpoint);

    void GenericSwitchOnInitialPress();
    void GenericSwitchOnShortRelease();

    void TriggerLightSwitchAction(LightSwitchAction action, bool isGroupCommand = false);
    void TriggerLevelControlAction(uint8_t stepMode, bool isGroupCommand = false);

    static LightSwitchMgr & GetInstance() { return sSwitch; }

    LightSwitchMgr();

    static void ButtonEventHandler(uint8_t button, uint8_t btnAction);

    static void GeneralEventHandler(AppEvent * aEvent);

protected:

    static void OnLongPressTimeout(Timer & timer);

    Timer * mLongPressTimer = nullptr;
    bool mDownPressed       = false;
    bool mResetWarning      = false;

private:
    static LightSwitchMgr sSwitch;
    void HandleLongPress();

    static void GenericSwitchWorkerFunction(intptr_t context);

    chip::EndpointId mLightSwitchEndpoint   = chip::kInvalidEndpointId;
    chip::EndpointId mGenericSwitchEndpoint = chip::kInvalidEndpointId;
    /**
     * @brief Button event processing function
     *        Function triggers a switch action sent to the CHIP task
     *
     * @param aEvent button event being processed
     */
    static void SwitchActionEventHandler(AppEvent * aEvent);
};
