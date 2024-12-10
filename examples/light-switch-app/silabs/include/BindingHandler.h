/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
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

#include "app-common/zap-generated/ids/Clusters.h"
#include "app-common/zap-generated/ids/Commands.h"
#include "lib/core/CHIPError.h"
#include <platform/CHIPDeviceLayer.h>
#include <app/clusters/bindings/bindings.h>
#include <variant>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters::LevelControl;

CHIP_ERROR InitBindingHandler();
void SwitchWorkerFunction(intptr_t context);
void BindingWorkerFunction(intptr_t context);


struct BindingCommandData
{
    chip::EndpointId localEndpointId = 1;
    chip::CommandId commandId;
    chip::ClusterId clusterId;
    bool isGroup = false;

    struct MoveToLevel
    {
        uint8_t level;
        DataModel::Nullable<uint16_t> transitionTime;
        chip::BitMask<OptionsBitmap> optionsMask;
        chip::BitMask<OptionsBitmap> optionsOverride;
    };

    struct Move
    {
        MoveModeEnum moveMode;
        DataModel::Nullable<uint8_t> rate;
        chip::BitMask<OptionsBitmap> optionsMask;
        chip::BitMask<OptionsBitmap> optionsOverride;
    };

    struct Step
    {
        StepModeEnum stepMode;
        uint8_t stepSize;
        DataModel::Nullable<uint16_t> transitionTime;
        chip::BitMask<OptionsBitmap> optionsMask;
        chip::BitMask<OptionsBitmap> optionsOverride;
    };

    struct Stop
    {
        chip::BitMask<OptionsBitmap> optionsMask;
        chip::BitMask<OptionsBitmap> optionsOverride;
    };

    // Use std::variant to replace the union
    std::variant<MoveToLevel, Move, Step, Stop> commandData;
};
