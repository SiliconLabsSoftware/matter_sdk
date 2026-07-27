/*
 *
 *    Copyright (c) 2024 Project CHIP Authors
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

/**********************************************************
 * Includes
 *********************************************************/

#include <stdbool.h>
#include <stdint.h>

#include "AppEvent.h"
#include "BaseApplication.h"
#include "FanControlManager.h"
#include <ble/Ble.h>
#include <cmsis_os2.h>
#include <lib/core/CHIPError.h>
#include <platform/CHIPDeviceLayer.h>

/**********************************************************
 * Defines
 *********************************************************/

// Application-defined error codes in the CHIP_ERROR space.
#define APP_ERROR_EVENT_QUEUE_FAILED CHIP_APPLICATION_ERROR(0x01)
#define APP_ERROR_CREATE_TASK_FAILED CHIP_APPLICATION_ERROR(0x02)
#define APP_ERROR_UNHANDLED_EVENT CHIP_APPLICATION_ERROR(0x03)
#define APP_ERROR_CREATE_TIMER_FAILED CHIP_APPLICATION_ERROR(0x04)
#define APP_ERROR_START_TIMER_FAILED CHIP_APPLICATION_ERROR(0x05)
#define APP_ERROR_STOP_TIMER_FAILED CHIP_APPLICATION_ERROR(0x06)

/**********************************************************
 * AppTask Declaration
 *********************************************************/

class AppTask : public BaseApplication
{

public:
    // Bring frequently-used cluster types into class scope. Available to derived classes
    // (e.g. AppTaskImpl, CustomerAppTask) without re-qualification.
    using FanModeEnum       = chip::app::Clusters::FanControl::FanModeEnum;
    using Percent           = chip::Percent;
    using StepDirectionEnum = chip::app::Clusters::FanControl::StepDirectionEnum;
    using Status            = chip::Protocols::InteractionModel::Status;

    static AppTask & GetAppTask() { return sAppTask; }

    /**
     * @brief AppTask task main loop function
     *
     * @param pvParameter FreeRTOS task parameter
     */
    static void AppTaskMain(void * pvParameter);

    CHIP_ERROR StartAppTask();

    /**
     * @brief Event handler when a button is pressed
     * Function posts an event for button processing
     *
     * @param buttonHandle APP_FAN_SWITCH or APP_FUNCTION_BUTTON
     * @param btnAction button action - SL_SIMPLE_BUTTON_PRESSED,
     *                  SL_SIMPLE_BUTTON_RELEASED or SL_SIMPLE_BUTTON_DISABLED
     */
    static void ButtonEventHandler(uint8_t button, uint8_t btnAction);

    void UpdateFanControlUI();

private:
    /**
     * @brief Reconcile PercentSetting or SpeedSetting when FanMode changes, depending on
     *        MultiSpeed feature support. Invoked from DMPostAttributeChangeCallback when
     *        MultiSpeed is disabled.
     */
    void HandleFanModeChange(FanModeEnum aNewFanMode);

    /**
     * @brief Mirror PercentSetting writes onto PercentCurrent (when not in Auto and not a no-op).
     *        FanMode is updated by the cluster on PercentSetting writes; the app does not re-derive it.
     */
    void HandlePercentSettingChange(uint8_t aNewPercentSetting);

    /**
     * @brief Derive FanMode from a PercentSetting value using SpeedMax-derived bands.
     *        Optional customer override hook; default handlers rely on cluster FanMode updates.
     */
    FanModeEnum DeriveFanModeFromPercent(Percent percent);

    /**
     * @brief Mirror SpeedSetting writes onto SpeedCurrent when MultiSpeed is enabled.
     */
    void HandleSpeedSettingChange(uint8_t aNewSpeedSetting);

    /**
     * @brief Read MultiSpeed support from the FanControl FeatureMap and refresh the
     *        sSupportsMultiSpeed cache. Call during InitFanControl() (including custom
     *        overrides) with the chip stack locked. Exposed for customer code; not overridable.
     */
    bool ReadSupportsMultiSpeedFromFeatureMap();

    /**
     * @brief Convert a discrete speed step into a percent using SpeedMax.
     */
    static uint8_t SpeedToPercent(uint8_t speed, uint8_t speedMax);

    chip::app::DataModel::Nullable<uint8_t> GetSpeedSetting();
    chip::app::DataModel::Nullable<Percent> GetPercentSetting();

    /**
     * @brief Write SpeedSetting synchronously on the Matter thread. No-op when MultiSpeed is
     *        disabled. Exposed for customer code; not overridable.
     */
    Status SetSpeedSetting(uint8_t aNewSpeedSetting);

    /**
     * @brief Schedule a PercentSetting write on the Matter thread. Exposed for customer code;
     *        not overridable.
     */
    Status SetPercentSetting(Percent aNewPercentSetting);

    /**
     * @brief PlatformMgr().ScheduleWork() callback that flushes pending FanControl attribute writes
     *        on the Matter thread. Exposed as a public API but not part of the CRTP override surface.
     */
    static void UpdateClusterState(intptr_t arg);

    /**
     * @brief Read the current FanMode attribute. Used by FanControlUI.
     */
    FanModeEnum GetFanMode();

protected:
    /** Override of `BaseApplication::AppInit()`. */
    CHIP_ERROR AppInit() override;

    /**
     * @brief Write FanMode when it differs from the data model. Used by default attribute
     *        handlers; exposed for customer overrides that reuse SDK sync behavior.
     */
    void SyncFanMode(FanModeEnum aNewFanMode);
};
