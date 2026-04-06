/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *    Copyright (c) 2019 Google LLC.
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

#include <stdbool.h>
#include <stdint.h>

#include "AppConfig.h"
#include "AppEvent.h"
#include "BaseApplication.h"
#include "LEDWidget.h"

#include <app/ConcreteAttributePath.h>
#include <app/clusters/window-covering-server/window-covering-server.h>
#include <ble/Ble.h>
#include <cmsis_os2.h>
#include <lib/core/CHIPError.h>
#include <platform/CHIPDeviceLayer.h>

#ifdef DISPLAY_ENABLED
#include <LcdPainter.h>
#endif

using namespace chip::app::Clusters::WindowCovering;

class AppTask : public BaseApplication
{
public:
    AppTask() = default;

    // --- Nested types (merged from WindowManager) ---

    struct Timer
    {
        typedef void (*Callback)(Timer & timer);

        Timer(uint32_t timeoutInMs, Callback callback, void * context);
        ~Timer();

        void Start();
        void Stop();
        void Timeout();

        Callback mCallback = nullptr;
        void * mContext     = nullptr;
        bool mIsActive      = false;

        osTimerId_t mHandler = nullptr;

    private:
        static void TimerCallback(void * timerCbArg);
    };

    struct Cover
    {
        enum ControlAction : uint8_t
        {
            Lift = 0,
            Tilt = 1
        };

        void Init(chip::EndpointId endpoint);
        void ScheduleControlAction(ControlAction action, bool setNewTarget);

        inline void LiftGoToTarget() { ScheduleControlAction(ControlAction::Lift, true); }
        inline void LiftContinueToTarget() { ScheduleControlAction(ControlAction::Lift, false); }
        inline void TiltGoToTarget() { ScheduleControlAction(ControlAction::Tilt, true); }
        inline void TiltContinueToTarget() { ScheduleControlAction(ControlAction::Tilt, false); }

        void PositionSet(chip::EndpointId endpointId, chip::Percent100ths position, ControlAction action);
        void UpdateTargetPosition(OperationalState direction, ControlAction action);

        Type CycleType();

        static void OnLiftTimeout(Timer & timer);
        static void OnTiltTimeout(Timer & timer);

        chip::EndpointId mEndpoint = 0;

        Timer * mLiftTimer = nullptr;
        Timer * mTiltTimer = nullptr;

        OperationalState mLiftOpState = OperationalState::Stall;
        OperationalState mTiltOpState = OperationalState::Stall;

        static void LiftUpdateWorker(intptr_t arg);
        static void TiltUpdateWorker(intptr_t arg);
    };

    struct CoverWorkData
    {
        Cover * cover     = nullptr;
        bool setNewTarget = false;
        CoverWorkData(Cover * c, bool t) : cover(c), setNewTarget(t) {}
        ~CoverWorkData() { cover = nullptr; }
    };

    // --- Public API ---

    static AppTask & GetAppTask(); // defined in CommonAppTask.cpp

    static void AppTaskMain(void * pvParameter);
    CHIP_ERROR StartAppTask();

    static void ButtonEventHandler(uint8_t button, uint8_t btnAction);

    void PostAttributeChange(chip::EndpointId endpoint, chip::AttributeId attributeId);
    void UpdateLED();

    static void GeneralEventHandler(AppEvent * aEvent);

#ifdef DISPLAY_ENABLED
    static void DrawUI(GLIB_Context_t * glibContext);
    void UpdateLCD();
#endif

    // --- DataModel callback virtual methods ---

    virtual void DmCallbackMatterPostAttributeChangeCallback(const chip::app::ConcreteAttributePath & attributePath, uint8_t type,
                                                             uint16_t size, uint8_t * value);

    virtual void DmCallbackMatterWindowCoveringClusterServerAttributeChangedCallback(
        const chip::app::ConcreteAttributePath & attributePath);

protected:
    CHIP_ERROR AppInit() override;

    Cover & GetCover();
    Cover * GetCover(chip::EndpointId endpoint);

    static void OnIconTimeout(Timer & timer);
    static void OnLongPressTimeout(Timer & timer);

private:
    void HandleLongPress();
    void DispatchEventAttributeChange(chip::EndpointId endpoint, chip::AttributeId attribute);

    Timer * mLongPressTimer = nullptr;
    bool mTiltMode          = false;
    bool mUpPressed         = false;
    bool mDownPressed       = false;
    bool mUpSuppressed      = false;
    bool mDownSuppressed    = false;
    bool mResetWarning      = false;

    Cover mCoverList[WINDOW_COVER_COUNT];
    uint8_t mCurrentCover = 0;

    LEDWidget mActionLED;
#ifdef DISPLAY_ENABLED
    Timer * mIconTimer = nullptr;
    LcdIcon mIcon      = LcdIcon::None;
#endif
};
