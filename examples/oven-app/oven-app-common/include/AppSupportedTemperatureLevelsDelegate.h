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

#include <app/clusters/temperature-control-server/supported-temperature-levels-manager.h>
#include <lib/core/CHIPError.h>
#include <lib/core/DataModelTypes.h>

namespace chip {
namespace app {
namespace Clusters {
namespace TemperatureControlledCabinet {

/**
 * Implementation of SupportedTemperatureLevelsIteratorDelegate for the oven app.
 * This class provides supported temperature levels for temperature control clusters.
 */
class AppSupportedTemperatureLevelsDelegate : public TemperatureControl::SupportedTemperatureLevelsIteratorDelegate
{
    struct EndpointPair
    {
        EndpointId mEndpointId;
        CharSpan * mTemperatureLevels;
        uint8_t mSize;

        EndpointPair(EndpointId aEndpointId, CharSpan * aTemperatureLevels, uint8_t aSize) :
            mEndpointId(aEndpointId), mTemperatureLevels(aTemperatureLevels), mSize(aSize)
        {}

        ~EndpointPair() {}
    };

    static CharSpan temperatureLevelOptions[3];

public:
    // Use a fixed size of 3 for oven app (multiple endpoints: 1, 4, 5)
    static const EndpointPair supportedOptionsByEndpoints[3];

    uint8_t Size() override;

    CHIP_ERROR Next(MutableCharSpan & item) override;

    ~AppSupportedTemperatureLevelsDelegate() {}
};

} // namespace TemperatureControlledCabinet
} // namespace Clusters
} // namespace app
} // namespace chip