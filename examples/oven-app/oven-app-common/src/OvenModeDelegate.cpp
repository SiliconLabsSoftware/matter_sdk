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

#include "OvenModeDelegate.h"
#include <app-common/zap-generated/cluster-objects.h>
#include <app/clusters/mode-base-server/mode-base-cluster-objects.h>
#include <lib/support/logging/CHIPLogging.h>
#include <lib/support/CodeUtils.h>

using namespace chip::app::Clusters;
using namespace chip::app::Clusters::OvenMode;
using namespace chip::app::Clusters::detail::Structs;

namespace chip {
namespace app {
namespace Clusters {
namespace TemperatureControlledCabinet {

// Static member definitions
const detail::Structs::ModeTagStruct::Type OvenModeDelegate::sModeTagsBake[1] = { { .value = to_underlying(ModeTag::kBake) } };

const detail::Structs::ModeTagStruct::Type OvenModeDelegate::sModeTagsConvection[1] = { { .value = to_underlying(
                                                                                              ModeTag::kConvection) } };

const detail::Structs::ModeTagStruct::Type OvenModeDelegate::sModeTagsGrill[1] = { { .value = to_underlying(ModeTag::kGrill) } };

const detail::Structs::ModeTagStruct::Type OvenModeDelegate::sModeTagsRoast[1] = { { .value = to_underlying(ModeTag::kRoast) } };

const detail::Structs::ModeTagStruct::Type OvenModeDelegate::sModeTagsClean[1] = { { .value = to_underlying(ModeTag::kClean) } };

const detail::Structs::ModeTagStruct::Type OvenModeDelegate::sModeTagsConvectionBake[1] = { { .value = to_underlying(
                                                                                                  ModeTag::kConvectionBake) } };

const detail::Structs::ModeTagStruct::Type OvenModeDelegate::sModeTagsConvectionRoast[1] = { { .value = to_underlying(
                                                                                                   ModeTag::kConvectionRoast) } };

const detail::Structs::ModeTagStruct::Type OvenModeDelegate::sModeTagsWarming[1] = { { .value =
                                                                                           to_underlying(ModeTag::kWarming) } };

const detail::Structs::ModeTagStruct::Type OvenModeDelegate::sModeTagsProofing[1] = { { .value =
                                                                                            to_underlying(ModeTag::kProofing) } };

const detail::Structs::ModeOptionStruct::Type OvenModeDelegate::skModeOptions[kModeCount] = {
    { .label    = CharSpan::fromCharString("Bake"),
      .mode     = kModeBake,
      .modeTags = DataModel::List<const detail::Structs::ModeTagStruct::Type>(sModeTagsBake) },
    { .label    = CharSpan::fromCharString("Convection"),
      .mode     = kModeConvection,
      .modeTags = DataModel::List<const detail::Structs::ModeTagStruct::Type>(sModeTagsConvection) },
    { .label    = CharSpan::fromCharString("Grill"),
      .mode     = kModeGrill,
      .modeTags = DataModel::List<const detail::Structs::ModeTagStruct::Type>(sModeTagsGrill) },
    { .label    = CharSpan::fromCharString("Roast"),
      .mode     = kModeRoast,
      .modeTags = DataModel::List<const detail::Structs::ModeTagStruct::Type>(sModeTagsRoast) },
    { .label    = CharSpan::fromCharString("Clean"),
      .mode     = kModeClean,
      .modeTags = DataModel::List<const detail::Structs::ModeTagStruct::Type>(sModeTagsClean) },
    { .label    = CharSpan::fromCharString("Convection Bake"),
      .mode     = kModeConvectionBake,
      .modeTags = DataModel::List<const detail::Structs::ModeTagStruct::Type>(sModeTagsConvectionBake) },
    { .label    = CharSpan::fromCharString("Convection Roast"),
      .mode     = kModeConvectionRoast,
      .modeTags = DataModel::List<const detail::Structs::ModeTagStruct::Type>(sModeTagsConvectionRoast) },
    { .label    = CharSpan::fromCharString("Warming"),
      .mode     = kModeWarming,
      .modeTags = DataModel::List<const detail::Structs::ModeTagStruct::Type>(sModeTagsWarming) },
    { .label    = CharSpan::fromCharString("Proofing"),
      .mode     = kModeProofing,
      .modeTags = DataModel::List<const detail::Structs::ModeTagStruct::Type>(sModeTagsProofing) }
};

using Protocols::InteractionModel::Status;

CHIP_ERROR OvenModeDelegate::Init()
{
    // Set the instance for the mode base delegate
    return CHIP_NO_ERROR;
}

void OvenModeDelegate::HandleChangeToMode(uint8_t NewMode, ModeBase::Commands::ChangeToModeResponse::Type & response)
{
    ChipLogProgress(Zcl, "OvenModeDelegate::HandleChangeToMode: NewMode=%d", NewMode);
    // Here, add code to handle the mode change
    response.status = to_underlying(ModeBase::StatusCode::kSuccess);
}

CHIP_ERROR OvenModeDelegate::GetModeLabelByIndex(uint8_t modeIndex, MutableCharSpan & label)
{
    if (modeIndex >= kModeCount)
    {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }
    return CopyCharSpanToMutableCharSpan(skModeOptions[modeIndex].label, label);
}

CHIP_ERROR OvenModeDelegate::GetModeValueByIndex(uint8_t modeIndex, uint8_t & value)
{
    if (modeIndex >= kModeCount)
    {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }
    value = skModeOptions[modeIndex].mode;
    return CHIP_NO_ERROR;
}

CHIP_ERROR OvenModeDelegate::GetModeTagsByIndex(uint8_t modeIndex, DataModel::List<detail::Structs::ModeTagStruct::Type> & tags)
{
  if (modeIndex >= MATTER_ARRAY_SIZE(skModeOptions))
  {
      return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
  }

  if (tags.size() < skModeOptions[modeIndex].modeTags.size())
  {
      return CHIP_ERROR_INVALID_ARGUMENT;
  }

  std::copy(skModeOptions[modeIndex].modeTags.begin(), skModeOptions[modeIndex].modeTags.end(), tags.begin());
  tags.reduce_size(skModeOptions[modeIndex].modeTags.size());

  return CHIP_NO_ERROR;
}

} // namespace TemperatureControlledCabinet
} // namespace Clusters
} // namespace app
} // namespace chip
