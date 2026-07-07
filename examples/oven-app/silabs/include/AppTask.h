/*
 *
 *    Copyright (c) 2025 Project CHIP Authors
 *    Copyright (c) 2025 Google LLC.
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

#include "FreeRTOS.h"
#include "timers.h" // provides FreeRTOS timer support
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/ConcreteAttributePath.h>
#include <app/clusters/bindings/BindingManager.h>
#include <ble/BLEEndPoint.h>
#include <lib/core/CHIPError.h>
#include <lib/core/DataModelTypes.h>
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

/**
 * @brief Data needed to propagate a CookTop state change to a single bound cluster
 *        (OnOff on the RangeHood Light endpoint or FanControl on the Extractor Hood endpoint).
 *        A separate context is allocated per cluster in CookTopBindingPropagateState(); on
 *        success ownership transfers to the binding manager and it is freed via the
 *        context-release handler.
 */
struct CookTopBindingContext
{
    chip::EndpointId localEndpointId;
    chip::ClusterId clusterId;
    bool cookTopOn;
};

/**********************************************************
 * AppTask Declaration
 *********************************************************/

class AppTask : public BaseApplication
{

public:
    AppTask() = default;

    /** @brief Returns the active app instance. */
    static AppTask & GetAppTask();

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
     * @param buttonHandle APP_FUNCTION_BUTTON
     * @param btnAction button action - SL_SIMPLE_BUTTON_PRESSED,
     *                  SL_SIMPLE_BUTTON_RELEASED or SL_SIMPLE_BUTTON_DISABLED
     */
    static void ButtonEventHandler(uint8_t button, uint8_t btnAction);

    /**
     * @brief PB1 Button event processing function for oven functionality
     *        Press and release will toggle cooktop and cook surface states
     *
     * @param aEvent button event being processed
     */
    static void OvenButtonHandler(AppEvent * aEvent);

    /**
     * @brief Handle oven-related AppEvents to update UI and LEDs.
     *
     * @param aEvent Oven Event to process
     */
    static void OvenActionHandler(AppEvent * aEvent);

    /**
     * @brief After reboot, send CookTop Off to bound rangehood peers once IP and DNS-SD are ready.
     */
    static void ConnectivityEventHandler(const chip::DeviceLayer::ChipDeviceEvent * event, intptr_t arg);

    /**
     * @brief Data model hook invoked after a cluster attribute changes; routes OnOff/OvenMode
     *        changes to OvenManager and logs Identify/TemperatureControl changes.
     */
    void DMPostAttributeChangeCallback(const chip::app::ConcreteAttributePath & attributePath, uint8_t type, uint16_t size,
                                       uint8_t * value);

    /**
     * @brief Schedules initialization of the binding manager on the Matter thread.
     */
    static void InitBindingHandler(intptr_t arg);

    /**
     * @brief Propagate the CookTop OnOff state to bound OnOff and FanControl peers.
     */
    static void CookTopBindingPropagateState(chip::EndpointId cookTopEndpoint, bool cookTopOn);

    /**
     * @brief Sends an OnOff cluster command to a bound peer to mirror CookTop state.
     */
    static void ProcessOnOffUnicast(bool cookTopOn, const chip::app::Clusters::Binding::TableEntry & binding,
                                   chip::Messaging::ExchangeManager * exchangeMgr, const chip::SessionHandle & sessionHandle);

    /**
     * @brief Writes the FanControl FanMode attribute on a bound peer to mirror CookTop state.
     */
    static void ProcessFanControlUnicast(bool cookTopOn, const chip::app::Clusters::Binding::TableEntry & binding,
                                       const chip::SessionHandle & sessionHandle);

    /**
     * @brief Binding manager callback invoked per bound device to propagate a CookTop state change.
     */
    static void BoundDeviceChangedHandler(const chip::app::Clusters::Binding::TableEntry & binding,
                                          chip::OperationalDeviceProxy * peerDevice, void * context);

protected:
    /**
     * @brief Override of BaseApplication::AppInit() virtual method, called by BaseApplication::Init()
     *
     * @return CHIP_ERROR
     */
    CHIP_ERROR AppInit() override;

    /**
     * @brief Updates the cluster state for button actions
     *
     * The context parameter contains the desired OnOff state encoded as an intptr_t
     * (0 == Off, non-zero == On). This value is provided by the caller via
     * PlatformMgr().ScheduleWork. The worker will set the OnOff attribute to
     * this boolean value.
     *
     * @param context bool value encoded as intptr_t
     */
    static void UpdateClusterState(intptr_t context);
};
