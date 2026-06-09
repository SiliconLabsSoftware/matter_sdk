/*
 *
 *    Copyright (c) 2024 Project CHIP Authors
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

#include <cstring>
#include <stdbool.h>
#include <stdint.h>
#include <utility>

#ifdef DISPLAY_ENABLED
#include "RefrigeratorUI.h"
#endif

#include "AppEvent.h"
#include "BaseApplication.h"
#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/ConcreteAttributePath.h>
#include <app/clusters/mode-base-server/mode-base-server.h>
#include <app/clusters/refrigerator-alarm-server/refrigerator-alarm-server.h>
#include <app/util/attribute-storage.h>
#include <app/util/config.h>
#include <ble/BLEEndPoint.h>
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

namespace chip {
namespace app {
namespace Clusters {

namespace RefrigeratorAndTemperatureControlledCabinetMode {

const uint8_t ModeNormal      = 0;
const uint8_t ModeRapidCool   = 1;
const uint8_t ModeRapidFreeze = 2;

class RefrigeratorAndTemperatureControlledCabinetModeDelegate : public ModeBase::Delegate
{
private:
    using ModeTagStructType                  = detail::Structs::ModeTagStruct::Type;
    ModeTagStructType modeTagsNormal[1]      = { { .value = to_underlying(ModeTag::kAuto) } };
    ModeTagStructType modeTagsRapidCool[1]   = { { .value = to_underlying(ModeTag::kRapidCool) } };
    ModeTagStructType modeTagsRapidFreeze[2] = { { .value = to_underlying(ModeBase::ModeTag::kMax) },
                                                 { .value = to_underlying(ModeTag::kRapidFreeze) } };

    const detail::Structs::ModeOptionStruct::Type kModeOptions[3] = {
        detail::Structs::ModeOptionStruct::Type{
            .label = "Normal"_span, .mode = ModeNormal, .modeTags = DataModel::List<const ModeTagStructType>(modeTagsNormal) },
        detail::Structs::ModeOptionStruct::Type{ .label    = "Rapid Cool"_span,
                                                 .mode     = ModeRapidCool,
                                                 .modeTags = DataModel::List<const ModeTagStructType>(modeTagsRapidCool) },
        detail::Structs::ModeOptionStruct::Type{ .label    = "Rapid Freeze"_span,
                                                 .mode     = ModeRapidFreeze,
                                                 .modeTags = DataModel::List<const ModeTagStructType>(modeTagsRapidFreeze) },
    };

    CHIP_ERROR Init() override;
    void HandleChangeToMode(uint8_t mode, ModeBase::Commands::ChangeToModeResponse::Type & response) override;
    CHIP_ERROR GetModeLabelByIndex(uint8_t modeIndex, MutableCharSpan & label) override;
    CHIP_ERROR GetModeValueByIndex(uint8_t modeIndex, uint8_t & value) override;
    CHIP_ERROR GetModeTagsByIndex(uint8_t modeIndex, DataModel::List<ModeTagStructType> & tags) override;

public:
    ~RefrigeratorAndTemperatureControlledCabinetModeDelegate() override = default;
};

ModeBase::Instance * Instance();

void Shutdown();

} // namespace RefrigeratorAndTemperatureControlledCabinetMode

} // namespace Clusters
} // namespace app
} // namespace chip

/**********************************************************
 * AppTask Declaration
 *********************************************************/

class AppTask : public BaseApplication
{

public:
    AppTask() = default;

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
     * @param buttonHandle APP_CONTROL_BUTTON or APP_FUNCTION_BUTTON
     * @param btnAction button action - SL_SIMPLE_BUTTON_PRESSED,
     *                  SL_SIMPLE_BUTTON_RELEASED or SL_SIMPLE_BUTTON_DISABLED
     */
    static void ButtonEventHandler(uint8_t button, uint8_t btnAction);

    /**
     * @brief Data model hook invoked when a cluster attribute changes.
     *        Merged from DataModelCallbacks.cpp (MatterPostAttributeChangeCallback).
     */
    void DMPostAttributeChangeCallback(const chip::app::ConcreteAttributePath & attributePath, uint8_t type, uint16_t size,
                                       uint8_t * value);

    /**
     * @brief Handle a TemperatureControl cluster attribute change. Merged from RefrigeratorManager.
     */
    void TempCtrlAttributeChangeHandler(chip::EndpointId endpointId, chip::AttributeId attributeId, uint8_t * value, uint16_t size);

    /**
     * @brief Handle a RefrigeratorAlarm cluster attribute change. Merged from RefrigeratorManager.
     */
    void RefAlarmAttributeChangeHandler(chip::EndpointId endpointId, chip::AttributeId attributeId, uint8_t * value, uint16_t size);

    uint8_t GetMode();
    int8_t GetCurrentTemp();
    int8_t SetMode();

private:
    static AppTask sAppTask;

    /**
     * @brief Override of BaseApplication::AppInit() virtual method, called by BaseApplication::Init()
     *
     * @return CHIP_ERROR
     */
    CHIP_ERROR AppInit() override;

    /**
     * @brief Refrigerator specific initialization. Merged from RefrigeratorManager::Init().
     *        Sets endpoint composition, tag lists, and the supported temperature levels delegate.
     */
    CHIP_ERROR InitRefrigerator();

    int8_t ConvertToPrintableTemp(int16_t temperature);

    /**
     * @brief PB0 Button event processing function
     *        Press and hold will trigger a factory reset timer start
     *        Press and release will restart BLEAdvertising if not commisionned
     *
     * @param aEvent button event being processed
     */
    static void ButtonHandler(AppEvent * aEvent);

    /**
     * @brief PB1 Button event processing function
     *        Function triggers an action sent to the CHIP task
     // TODO: Action for refrigerator is not decided yet
     *
     * @param aEvent button event being processed
     */
    static void RefrigeratorActionEventHandler(AppEvent * aEvent);

    // Refrigerator state, merged from RefrigeratorManager.
    int16_t mCurrentMode;
    int16_t mOnMode;

    int16_t mTemperatureSetpoint;
    int16_t mMinTemperature;
    int16_t mMaxTemperature;

    chip::app::Clusters::RefrigeratorAlarm::AlarmBitmap mMask;
    chip::app::Clusters::RefrigeratorAlarm::AlarmBitmap mState;
    chip::app::Clusters::RefrigeratorAlarm::AlarmBitmap mSupported;
};
