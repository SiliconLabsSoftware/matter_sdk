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

#include "AppTask.h"
#include "CRTPHelpers.h"

/**
 * @brief CRTP base for refrigerator AppTask, exposing override hooks for customizable APIs.
 *
 * Each public method dispatches to `Derived::*Impl()`. Overrides are optional: default
 * `*Impl()` implementations in the private section forward to `AppTask`. Override in
 * `Derived` only for the behaviors you want to customize.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class AppTaskImpl : public AppTask
{
public:
    // Common AppTask bring up
    CHIP_ERROR AppInit() override { CRTP_OPTIONAL_DISPATCH(AppTaskImpl, Derived, AppInitImpl); }

    // Refrigerator specific initialization
    CHIP_ERROR InitRefrigerator() { CRTP_OPTIONAL_DISPATCH(AppTaskImpl, Derived, InitRefrigeratorImpl); }

    // Handle button press
    static void ButtonEventHandler(uint8_t button, uint8_t btnAction)
    {
        CRTP_OPTIONAL_STATIC_DISPATCH(AppTaskImpl, Derived, ButtonEventHandlerImpl, button, btnAction);
    }

    // Data model hook invoked when a cluster attribute changes
    void DMPostAttributeChangeCallback(const chip::app::ConcreteAttributePath & attributePath, uint8_t type, uint16_t size,
                                       uint8_t * value)
    {
        CRTP_OPTIONAL_VOID_DISPATCH(AppTaskImpl, Derived, DMPostAttributeChangeCallbackImpl, attributePath, type, size, value);
    }

    // Data model hook to construct the RefrigeratorAndTemperatureControlledCabinetMode cluster on the given endpoint
    void DMCabinetModeClusterInit(chip::EndpointId endpointId)
    {
        CRTP_OPTIONAL_VOID_DISPATCH(AppTaskImpl, Derived, DMCabinetModeClusterInitImpl, endpointId);
    }

    // CabinetMode delegate datamodel hooks. Default `*Impl()` forwards to
    // `AppTask::DMCabinetMode*`. Override in `Derived` to customize the mode
    // list behavior.
    CHIP_ERROR DMCabinetModeInit() { CRTP_OPTIONAL_DISPATCH(AppTaskImpl, Derived, DMCabinetModeInitImpl); }

    void DMCabinetModeHandleChangeToMode(uint8_t newMode, uint8_t currentMode,
                                         chip::app::Clusters::ModeBase::Commands::ChangeToModeResponse::Type & response)
    {
        CRTP_OPTIONAL_VOID_DISPATCH(AppTaskImpl, Derived, DMCabinetModeHandleChangeToModeImpl, newMode, currentMode, response);
    }

    CHIP_ERROR DMCabinetModeGetModeLabelByIndex(uint8_t modeIndex, chip::MutableCharSpan & label)
    {
        CRTP_OPTIONAL_DISPATCH_ARGS(AppTaskImpl, Derived, DMCabinetModeGetModeLabelByIndexImpl, modeIndex, label);
    }

    CHIP_ERROR DMCabinetModeGetModeValueByIndex(uint8_t modeIndex, uint8_t & value)
    {
        CRTP_OPTIONAL_DISPATCH_ARGS(AppTaskImpl, Derived, DMCabinetModeGetModeValueByIndexImpl, modeIndex, value);
    }

    CHIP_ERROR DMCabinetModeGetModeTagsByIndex(
        uint8_t modeIndex, chip::app::DataModel::List<chip::app::Clusters::detail::Structs::ModeTagStruct::Type> & tags)
    {
        CRTP_OPTIONAL_DISPATCH_ARGS(AppTaskImpl, Derived, DMCabinetModeGetModeTagsByIndexImpl, modeIndex, tags);
    }

private:
    friend Derived;

    // Default *Impl() hooks, each forwards to the matching AppTask method
    // Override the corresponding hook in CustomerAppTask to customize behavior

    CHIP_ERROR AppInitImpl() { return AppTask::AppInit(); }

    CHIP_ERROR InitRefrigeratorImpl() { return AppTask::InitRefrigerator(); }

    void ButtonEventHandlerImpl(uint8_t button, uint8_t btnAction) { AppTask::ButtonEventHandler(button, btnAction); }

    void DMPostAttributeChangeCallbackImpl(const chip::app::ConcreteAttributePath & attributePath, uint8_t type, uint16_t size,
                                           uint8_t * value)
    {
        AppTask::DMPostAttributeChangeCallback(attributePath, type, size, value);
    }

    void DMCabinetModeClusterInitImpl(chip::EndpointId endpointId) { AppTask::DMCabinetModeClusterInit(endpointId); }

    // CabinetMode delegate DM defaults, each forwards to the matching AppTask::DMCabinetMode* method

    CHIP_ERROR DMCabinetModeInitImpl() { return AppTask::DMCabinetModeInit(); }

    void DMCabinetModeHandleChangeToModeImpl(uint8_t newMode, uint8_t currentMode,
                                             chip::app::Clusters::ModeBase::Commands::ChangeToModeResponse::Type & response)
    {
        AppTask::DMCabinetModeHandleChangeToMode(newMode, currentMode, response);
    }

    CHIP_ERROR DMCabinetModeGetModeLabelByIndexImpl(uint8_t modeIndex, chip::MutableCharSpan & label)
    {
        return AppTask::DMCabinetModeGetModeLabelByIndex(modeIndex, label);
    }

    CHIP_ERROR DMCabinetModeGetModeValueByIndexImpl(uint8_t modeIndex, uint8_t & value)
    {
        return AppTask::DMCabinetModeGetModeValueByIndex(modeIndex, value);
    }

    CHIP_ERROR DMCabinetModeGetModeTagsByIndexImpl(
        uint8_t modeIndex, chip::app::DataModel::List<chip::app::Clusters::detail::Structs::ModeTagStruct::Type> & tags)
    {
        return AppTask::DMCabinetModeGetModeTagsByIndex(modeIndex, tags);
    }
};
