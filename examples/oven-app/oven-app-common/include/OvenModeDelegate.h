/*
 *
 *    Copyright (c) 2025 Project CHIP Authors
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

#include <app-common/zap-generated/cluster-objects.h> // provides detail::Structs::ModeTagStruct::Type and ModeOptionStruct::Type
#include <app/clusters/mode-base-server/mode-base-server.h>
#include <app/data-model/List.h>
#include <lib/core/CHIPError.h>
#include <lib/core/DataModelTypes.h>
#include <lib/support/Span.h>

namespace chip {
namespace app {
namespace Clusters {
namespace TemperatureControlledCabinet {

/// This is an application level delegate to handle Oven commands according to the specific business logic.
class OvenModeDelegate : public ModeBase::Delegate
{
public:
    // Oven mode constants - made static and public for potential reuse
    static constexpr uint8_t kModeBake            = 0;
    static constexpr uint8_t kModeConvection      = 1;
    static constexpr uint8_t kModeGrill           = 2;
    static constexpr uint8_t kModeRoast           = 3;
    static constexpr uint8_t kModeClean           = 4;
    static constexpr uint8_t kModeConvectionBake  = 5;
    static constexpr uint8_t kModeConvectionRoast = 6;
    static constexpr uint8_t kModeWarming         = 7;
    static constexpr uint8_t kModeProofing        = 8;
    static constexpr uint8_t kModeCount           = 9;

    OvenModeDelegate(EndpointId endpointId) : mEndpointId(endpointId) {}

    // ModeBase::Delegate interface
    CHIP_ERROR Init() override;
    void HandleChangeToMode(uint8_t mode, ModeBase::Commands::ChangeToModeResponse::Type & response) override;
    CHIP_ERROR GetModeLabelByIndex(uint8_t modeIndex, MutableCharSpan & label) override;
    CHIP_ERROR GetModeValueByIndex(uint8_t modeIndex, uint8_t & value) override;
    CHIP_ERROR GetModeTagsByIndex(uint8_t modeIndex, DataModel::List<detail::Structs::ModeTagStruct::Type> & tags) override;

private:
    EndpointId mEndpointId;

    // Static arrays moved to implementation file to reduce header size
    static const detail::Structs::ModeTagStruct::Type sModeTagsBake[];
    static const detail::Structs::ModeTagStruct::Type sModeTagsConvection[];
    static const detail::Structs::ModeTagStruct::Type sModeTagsGrill[];
    static const detail::Structs::ModeTagStruct::Type sModeTagsRoast[];
    static const detail::Structs::ModeTagStruct::Type sModeTagsClean[];
    static const detail::Structs::ModeTagStruct::Type sModeTagsConvectionBake[];
    static const detail::Structs::ModeTagStruct::Type sModeTagsConvectionRoast[];
    static const detail::Structs::ModeTagStruct::Type sModeTagsWarming[];
    static const detail::Structs::ModeTagStruct::Type sModeTagsProofing[];

    static const detail::Structs::ModeOptionStruct::Type skModeOptions[];
};

} // namespace TemperatureControlledCabinet
} // namespace Clusters
} // namespace app
} // namespace chip
